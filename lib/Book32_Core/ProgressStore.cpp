#include "ProgressStore.h"
#include "Book32FS.h"
#include "BookMeta.h"

// Todos os métodos públicos abrem com um Book32Guard. load() e save() ficam
// sem guarda de propósito: são privados e só são alcançados a partir de um
// método público que já detém o mutex (que é recursivo, por isso mesmo que
// voltasse a tomá-lo não haveria bloqueio).

static const char* PROGRESS_PATH = "/reader_progress.json";
static const char* PROGRESS_TMP_PATH = "/reader_progress.tmp";

// Read capacity follows the file; write capacity follows the entry count.
static size_t readCapacityFor(size_t fileSize) {
    size_t cap = fileSize * 2 + 1024;
    if (cap > 32768) cap = 32768;
    return cap;
}

static size_t writeCapacityFor(size_t entries) {
    size_t cap = 512 + entries * 192;
    if (cap > 32768) cap = 32768;
    return cap;
}

ProgressStore& ProgressStore::getInstance() {
    static ProgressStore instance;
    return instance;
}

void ProgressStore::begin() {
    Book32Guard guard(_mutex);
    if (_loaded) return;
    _loaded = true;  // set first: a failed load leaves an empty, usable store
    load();
}

bool ProgressStore::load() {
    _books.clear();
    _lastBook = "";
    _resumeOnBoot = false;
    _seq = 0;

    if (!EbookFS.exists(PROGRESS_PATH)) return false;

    File file = EbookFS.open(PROGRESS_PATH, "r");
    if (!file) return false;

    DynamicJsonDocument doc(readCapacityFor(file.size()));
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
        Serial.printf("ProgressStore: %s unreadable (%s) — starting empty\n",
                      PROGRESS_PATH, error.c_str());
        return false;
    }

    int schema = doc["schema"] | 1;
    if (!isSupportedSchema(schema)) {
        Serial.printf("ProgressStore: schema %d not supported — ignoring file\n", schema);
        return false;
    }

    _seq = doc["seq"] | 0UL;
    _resumeOnBoot = doc["resumeOnBoot"] | false;

    // v1 stored paths ("/livro.epub"); v2 stores original filenames.
    auto resolve = [](const String& name) { return getOriginalFilename(name); };
    _lastBook = migrateProgressKey<String>(String(doc["lastBook"] | ""), resolve);

    JsonObject books = doc["books"].as<JsonObject>();
    if (!books.isNull()) {
        for (JsonPair pair : books) {
            String key = migrateProgressKey<String>(String(pair.key().c_str()), resolve);
            if (key.length() == 0) continue;

            JsonObject entry = pair.value().as<JsonObject>();
            BookProgress p;
            p.chapter = entry["chapter"] | 0;
            p.nodeIndex = entry["nodeIndex"] | 0;
            p.charOffset = entry["charOffset"] | 0;
            p.globalPage = entry["globalPage"] | 1;
            p.seq = entry["seq"] | 0UL;
            p.pending = entry["pending"] | false;
            if (p.seq > _seq) _seq = p.seq;

            // A v1 file could hold both "/livro.epub" and "livro.epub" after a
            // rename; keep whichever is further ahead.
            auto existing = _books.find(key);
            if (existing == _books.end() || p.globalPage > existing->second.globalPage) {
                _books[key] = p;
            }
        }
    }

    if (schema < PROGRESS_SCHEMA_CURRENT) {
        Serial.printf("ProgressStore: migrating schema %d -> %d (%u books)\n",
                      schema, PROGRESS_SCHEMA_CURRENT, (unsigned)_books.size());
        save();
    }
    return true;
}

bool ProgressStore::save() {
    DynamicJsonDocument doc(writeCapacityFor(_books.size()));
    doc["schema"] = PROGRESS_SCHEMA_CURRENT;
    doc["seq"] = _seq;
    doc["lastBook"] = _lastBook;
    doc["resumeOnBoot"] = _resumeOnBoot;

    JsonObject books = doc.createNestedObject("books");
    for (const auto& kv : _books) {
        JsonObject entry = books.createNestedObject(kv.first);
        entry["chapter"] = kv.second.chapter;
        entry["nodeIndex"] = kv.second.nodeIndex;
        entry["charOffset"] = kv.second.charOffset;
        entry["globalPage"] = kv.second.globalPage;
        entry["seq"] = kv.second.seq;
        if (kv.second.pending) entry["pending"] = true;
    }

    if (doc.overflowed()) {
        // Better to refuse the write than to overwrite a good file with a
        // truncated one — that was exactly the v1 failure mode.
        Serial.println("ProgressStore: document overflowed — write refused");
        return false;
    }

    File out = EbookFS.open(PROGRESS_TMP_PATH, FILE_WRITE);
    if (!out) {
        Serial.println("ProgressStore: cannot open temp file");
        return false;
    }
    size_t written = serializeJson(doc, out);
    out.flush();
    out.close();
    if (written == 0) {
        EbookFS.remove(PROGRESS_TMP_PATH);
        Serial.println("ProgressStore: serialisation wrote nothing");
        return false;
    }

    // littlefs rename replaces the destination atomically; the remove+retry is
    // only for ports where it refuses an existing target.
    if (!EbookFS.rename(PROGRESS_TMP_PATH, PROGRESS_PATH)) {
        EbookFS.remove(PROGRESS_PATH);
        if (!EbookFS.rename(PROGRESS_TMP_PATH, PROGRESS_PATH)) {
            EbookFS.remove(PROGRESS_TMP_PATH);
            Serial.println("ProgressStore: rename failed — progress not saved");
            return false;
        }
    }
    return true;
}

bool ProgressStore::get(const String& originalName, BookProgress& out) {
    Book32Guard guard(_mutex);
    begin();
    auto it = _books.find(originalName);
    if (it == _books.end()) return false;
    out = it->second;
    return true;
}

void ProgressStore::set(const String& originalName, const BookProgress& progress) {
    Book32Guard guard(_mutex);
    begin();
    if (originalName.length() == 0) return;
    BookProgress p = progress;
    p.seq = ++_seq;
    p.pending = false;  // we only get here by actually reading the book
    _books[originalName] = p;
    save();
}

void ProgressStore::remove(const String& originalName) {
    Book32Guard guard(_mutex);
    begin();
    bool changed = _books.erase(originalName) > 0;
    if (_lastBook == originalName) {
        _lastBook = "";
        _resumeOnBoot = false;
        changed = true;
    }
    if (changed) save();
}

void ProgressStore::setLast(const String& originalName, bool resumeOnBoot) {
    Book32Guard guard(_mutex);
    begin();
    if (_lastBook == originalName && _resumeOnBoot == resumeOnBoot) return;
    _lastBook = originalName;
    _resumeOnBoot = resumeOnBoot;
    save();
}

void ProgressStore::setResumeOnBoot(bool resume) {
    Book32Guard guard(_mutex);
    begin();
    if (_resumeOnBoot == resume) return;
    _resumeOnBoot = resume;
    save();
}

String ProgressStore::lastBook() {
    Book32Guard guard(_mutex);
    begin();
    return _lastBook;
}

bool ProgressStore::resumeOnBoot() {
    Book32Guard guard(_mutex);
    begin();
    return _resumeOnBoot;
}

void ProgressStore::reconcile(const std::vector<String>& presentOriginalNames) {
    Book32Guard guard(_mutex);
    begin();
    bool changed = false;

    for (auto it = _books.begin(); it != _books.end();) {
        bool exists = false;
        for (const String& name : presentOriginalNames) {
            if (name == it->first) { exists = true; break; }
        }

        if (shouldPrune(it->second, exists)) {
            it = _books.erase(it);
            changed = true;
            continue;
        }
        if (syncPending(it->second, exists)) changed = true;
        ++it;
    }

    if (_lastBook.length() > 0) {
        bool exists = false;
        for (const String& name : presentOriginalNames) {
            if (name == _lastBook) { exists = true; break; }
        }
        if (!exists && _books.find(_lastBook) == _books.end()) {
            _lastBook = "";
            _resumeOnBoot = false;
            changed = true;
        }
    }

    if (changed) save();
}

void ProgressStore::clearAll() {
    Book32Guard guard(_mutex);
    begin();
    _books.clear();
    _lastBook = "";
    _resumeOnBoot = false;
    _seq++;
    save();
}

size_t ProgressStore::count() {
    Book32Guard guard(_mutex);
    begin();
    return _books.size();
}

void ProgressStore::fillExportJson(JsonObject dest) {
    Book32Guard guard(_mutex);
    begin();
    for (const auto& kv : _books) {
        JsonObject entry = dest.createNestedObject(kv.first);
        entry["chapter"] = kv.second.chapter;
        entry["nodeIndex"] = kv.second.nodeIndex;
        entry["charOffset"] = kv.second.charOffset;
        entry["globalPage"] = kv.second.globalPage;
    }
}

// Original names of every .epub currently on flash. Built once per import: the
// per-entry alternative re-read /books_meta.json for each book, and dozens of
// sequential LittleFS reads on the async web task would stall the server.
static void collectPresentOriginalNames(std::map<String, bool>& present) {
    std::map<String, String> metadata;
    loadBookMetadata(metadata);

    File root = EbookFS.open("/");
    if (!root || !root.isDirectory()) return;

    File file = root.openNextFile();
    while (file) {
        String name = file.name();
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);

        String lower = name;
        lower.toLowerCase();
        if (lower.endsWith(".epub")) {
            auto meta = metadata.find(name);
            present[(meta != metadata.end()) ? meta->second : name] = true;
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
}

ImportReport ProgressStore::applyImportedJson(JsonObjectConst src) {
    Book32Guard guard(_mutex);
    begin();
    ImportReport report;

    if (src.isNull()) {
        report.error = "bundle sem seccao progress";
        return report;
    }

    std::map<String, bool> present;
    collectPresentOriginalNames(present);

    for (JsonPairConst pair : src) {
        String key = pair.key().c_str();
        if (key.length() == 0) continue;

        JsonObjectConst entry = pair.value().as<JsonObjectConst>();
        if (entry.isNull()) { report.skipped++; continue; }

        BookProgress imported;
        imported.chapter = entry["chapter"] | 0;
        imported.nodeIndex = entry["nodeIndex"] | 0;
        imported.charOffset = entry["charOffset"] | 0;
        imported.globalPage = entry["globalPage"] | 1;

        // The .epub may not be here yet — that is what `pending` protects.
        bool fileExists = present.find(key) != present.end();

        auto it = _books.find(key);
        const BookProgress* local = (it == _books.end()) ? nullptr : &it->second;

        BookProgress merged;
        MergeResult result = mergeProgress(local, imported, fileExists, merged);
        merged.seq = ++_seq;

        switch (result) {
            case MergeResult::Added:
                report.added++;
                if (merged.pending) report.pending++;
                break;
            case MergeResult::Merged:
                report.merged++;
                break;
            case MergeResult::KeptLocal:
                report.skipped++;
                break;
        }
        _books[key] = merged;
    }

    report.ok = save();
    if (!report.ok) report.error = "falha ao gravar o estado";
    return report;
}
