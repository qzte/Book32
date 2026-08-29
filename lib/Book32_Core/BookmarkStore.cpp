#include "BookmarkStore.h"
#include "Book32FS.h"

static const char* BOOKMARKS_PATH = "/bookmarks.json";
static const char* BOOKMARKS_TMP_PATH = "/bookmarks.tmp";

// Same shape as ProgressStore's capacity helpers: read capacity follows the
// file, write capacity follows the entry count (label + four ints + seq).
static size_t readCapacityFor(size_t fileSize) {
    size_t cap = fileSize * 2 + 1024;
    if (cap > 32768) cap = 32768;
    return cap;
}

static size_t writeCapacityFor(size_t entries) {
    size_t cap = 512 + entries * 128;
    if (cap > 32768) cap = 32768;
    return cap;
}

BookmarkStore& BookmarkStore::getInstance() {
    static BookmarkStore instance;
    return instance;
}

void BookmarkStore::begin() {
    Book32Guard guard(_mutex);
    if (_loaded) return;
    _loaded = true; // set first: a failed load leaves an empty, usable store
    load();
}

bool BookmarkStore::load() {
    _books.clear();
    _seq = 0;

    if (!EbookFS.exists(BOOKMARKS_PATH)) return false;

    File file = EbookFS.open(BOOKMARKS_PATH, "r");
    if (!file) return false;

    DynamicJsonDocument doc(readCapacityFor(file.size()));
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) {
        Serial.printf("BookmarkStore: %s unreadable (%s) — starting empty\n", BOOKMARKS_PATH, error.c_str());
        return false;
    }

    _seq = doc["seq"] | 0UL;

    JsonObject books = doc["books"].as<JsonObject>();
    if (!books.isNull()) {
        for (JsonPair pair : books) {
            String key(pair.key().c_str());
            if (key.length() == 0) continue;

            JsonArray arr = pair.value().as<JsonArray>();
            if (arr.isNull()) continue;

            std::vector<Bookmark> marks;
            for (JsonObject entry : arr) {
                Bookmark b;
                b.chapter = entry["chapter"] | 0;
                b.nodeIndex = entry["nodeIndex"] | 0;
                b.charOffset = entry["charOffset"] | 0;
                b.globalPage = entry["globalPage"] | 1;
                b.seq = entry["seq"] | 0UL;
                b.label = entry["label"] | "";
                if (b.seq > _seq) _seq = b.seq;
                marks.push_back(b);
            }
            if (!marks.empty()) _books[key] = marks;
        }
    }
    return true;
}

bool BookmarkStore::save() {
    size_t entries = 0;
    for (const auto& kv : _books)
        entries += kv.second.size();

    DynamicJsonDocument doc(writeCapacityFor(entries));
    doc["schema"] = 1;
    doc["seq"] = _seq;

    JsonObject books = doc.createNestedObject("books");
    for (const auto& kv : _books) {
        JsonArray arr = books.createNestedArray(kv.first);
        for (const Bookmark& b : kv.second) {
            JsonObject entry = arr.createNestedObject();
            entry["chapter"] = b.chapter;
            entry["nodeIndex"] = b.nodeIndex;
            entry["charOffset"] = b.charOffset;
            entry["globalPage"] = b.globalPage;
            entry["seq"] = b.seq;
            entry["label"] = b.label;
        }
    }

    if (doc.overflowed()) {
        // Better to refuse the write than overwrite a good file with a
        // truncated one — same rule ProgressStore follows.
        Serial.println("BookmarkStore: document overflowed — write refused");
        return false;
    }

    File out = EbookFS.open(BOOKMARKS_TMP_PATH, FILE_WRITE);
    if (!out) {
        Serial.println("BookmarkStore: cannot open temp file");
        return false;
    }
    size_t written = serializeJson(doc, out);
    out.flush();
    out.close();
    if (written == 0) {
        EbookFS.remove(BOOKMARKS_TMP_PATH);
        Serial.println("BookmarkStore: serialisation wrote nothing");
        return false;
    }

    if (!EbookFS.rename(BOOKMARKS_TMP_PATH, BOOKMARKS_PATH)) {
        EbookFS.remove(BOOKMARKS_PATH);
        if (!EbookFS.rename(BOOKMARKS_TMP_PATH, BOOKMARKS_PATH)) {
            EbookFS.remove(BOOKMARKS_TMP_PATH);
            Serial.println("BookmarkStore: rename failed — bookmark not saved");
            return false;
        }
    }
    return true;
}

std::vector<Bookmark> BookmarkStore::list(const String& originalName) {
    Book32Guard guard(_mutex);
    begin();
    auto it = _books.find(originalName);
    if (it == _books.end()) return {};
    return it->second;
}

unsigned long BookmarkStore::add(const String& originalName, const String& label, int chapter, int nodeIndex,
                                 int charOffset, int globalPage) {
    Book32Guard guard(_mutex);
    begin();
    if (originalName.length() == 0) return 0;

    std::vector<Bookmark>& marks = _books[originalName];
    if (marks.size() >= MAX_BOOKMARKS_PER_BOOK) return 0;

    Bookmark b;
    b.chapter = chapter;
    b.nodeIndex = nodeIndex;
    b.charOffset = charOffset;
    b.globalPage = globalPage;
    b.seq = ++_seq;
    b.label = label.substring(0, MAX_LABEL_LEN);
    marks.push_back(b);

    if (!save()) {
        marks.pop_back();
        if (marks.empty()) _books.erase(originalName);
        return 0;
    }
    return b.seq;
}

bool BookmarkStore::remove(const String& originalName, unsigned long seq) {
    Book32Guard guard(_mutex);
    begin();
    auto it = _books.find(originalName);
    if (it == _books.end()) return false;

    std::vector<Bookmark>& marks = it->second;
    for (size_t i = 0; i < marks.size(); ++i) {
        if (marks[i].seq == seq) {
            marks.erase(marks.begin() + i);
            if (marks.empty()) _books.erase(it);
            save();
            return true;
        }
    }
    return false;
}

void BookmarkStore::reconcile(const std::vector<String>& presentOriginalNames) {
    Book32Guard guard(_mutex);
    begin();
    bool changed = false;

    for (auto it = _books.begin(); it != _books.end();) {
        bool exists = false;
        for (const String& name : presentOriginalNames) {
            if (name == it->first) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            it = _books.erase(it);
            changed = true;
            continue;
        }
        ++it;
    }

    if (changed) save();
}

size_t BookmarkStore::count(const String& originalName) {
    Book32Guard guard(_mutex);
    begin();
    auto it = _books.find(originalName);
    return (it == _books.end()) ? 0 : it->second.size();
}
