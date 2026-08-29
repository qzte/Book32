#include "ProgressStore.h"
#include "Book32FS.h"
#include "BookMeta.h"
#include "TimeMgr.h"

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
    // 192 -> 288: the v3 entry carries an override name and three dates.
    size_t cap = 512 + entries * 288;
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
            // v3. Absent in a v2 file, which is exactly the migration: an old
            // entry becomes Auto with unknown dates and behaves as before.
            StatusOverride parsed = StatusOverride::Auto;
            if (parseOverride(entry["override"] | "", parsed)) p.override = parsed;
            p.startedAt = entry["startedAt"] | 0UL;
            p.finishedAt = entry["finishedAt"] | 0UL;
            p.lastReadAt = entry["lastReadAt"] | 0UL;
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
        // Omitted when they carry no information, to keep the file (and the
        // 32 KB write budget) close to its v2 size for a library that has
        // never been marked or dated.
        if (kv.second.override != StatusOverride::Auto) entry["override"] = overrideKey(kv.second.override);
        if (kv.second.startedAt) entry["startedAt"] = kv.second.startedAt;
        if (kv.second.finishedAt) entry["finishedAt"] = kv.second.finishedAt;
        if (kv.second.lastReadAt) entry["lastReadAt"] = kv.second.lastReadAt;
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

void ProgressStore::set(const String& originalName, const BookProgress& progress, int totalPages) {
    Book32Guard guard(_mutex);
    begin();
    if (originalName.length() == 0) return;
    BookProgress p = progress;
    p.seq = ++_seq;
    p.pending = false;  // we only get here by actually reading the book

    // Status and dates belong to the book, not to the position the caller just
    // handed us. Carry them over from the stored entry (see the header): a
    // fresh BookProgress from AppReader has them all zeroed, and taking that
    // literally would wipe a manual mark on the next page turn.
    auto it = _books.find(originalName);
    if (it != _books.end()) {
        p.override = it->second.override;
        p.startedAt = it->second.startedAt;
        p.finishedAt = it->second.finishedAt;
        p.lastReadAt = it->second.lastReadAt;
    } else {
        p.override = StatusOverride::Auto;
        p.startedAt = 0;
        p.finishedAt = 0;
        p.lastReadAt = 0;
    }

    // Zero means the clock is unknown (no NTP since the last power cut). Every
    // date then stays as it was: an absent date is recoverable, an invented one
    // is not. See TimeMgr.h.
    uint32_t now = TimeMgr::getInstance().nowOrZero();
    if (now != 0) {
        if (p.startedAt == 0) p.startedAt = now;
        p.lastReadAt = now;

        // The finish is dated the first time the position crosses the
        // threshold, and never re-dated afterwards — paging back and forth
        // through the last chapter must not keep moving the date.
        //
        // Only while the status is actually being derived. Under a manual mark
        // the user has already said where the book stands: dating a finish
        // under "Reading" would put "Finished <date>" on a book the UI badges
        // as still being read, and under "Read" the date is setOverride's.
        if (p.finishedAt == 0 && totalPages > 0 && p.override == StatusOverride::Auto) {
            BookStatusView view = deriveStatus(true, StatusOverride::Auto, p.globalPage, totalPages);
            if (view.status == BookStatus::Read) p.finishedAt = now;
        }
    }

    _books[originalName] = p;
    save();
}

bool ProgressStore::setOverride(const String& originalName, StatusOverride override, uint32_t atEpoch) {
    Book32Guard guard(_mutex);
    begin();
    if (originalName.length() == 0) return false;

    auto it = _books.find(originalName);
    BookProgress p;
    if (it != _books.end()) {
        p = it->second;
    }
    // No entry means the book was never opened here. Marking it read is
    // legitimate — you read it somewhere else — and the entry that gets created
    // simply carries no position.

    p.override = override;
    p.seq = ++_seq;

    if (override == StatusOverride::Read) {
        // Only when unset: a date already recorded by the device (or imported
        // from another one) is the better evidence of when it was actually
        // finished.
        if (p.finishedAt == 0 && atEpoch != 0) p.finishedAt = atEpoch;
    } else if (override == StatusOverride::Unread) {
        // Marking a book unread is how a re-read starts, and a book waiting to
        // be read has no finish date. `startedAt` and `lastReadAt` survive: the
        // first read did happen, and losing that is not what was asked for.
        p.finishedAt = 0;
    }

    _books[originalName] = p;
    return save();
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
        // Status and dates are portable between devices and belong in the
        // bundle, unlike `pending` and `seq`, which are device-local.
        if (kv.second.override != StatusOverride::Auto) entry["override"] = overrideKey(kv.second.override);
        if (kv.second.startedAt) entry["startedAt"] = kv.second.startedAt;
        if (kv.second.finishedAt) entry["finishedAt"] = kv.second.finishedAt;
        if (kv.second.lastReadAt) entry["lastReadAt"] = kv.second.lastReadAt;
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
        StatusOverride importedOverride = StatusOverride::Auto;
        if (parseOverride(entry["override"] | "", importedOverride)) imported.override = importedOverride;
        imported.startedAt = entry["startedAt"] | 0UL;
        imported.finishedAt = entry["finishedAt"] | 0UL;
        imported.lastReadAt = entry["lastReadAt"] | 0UL;

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
