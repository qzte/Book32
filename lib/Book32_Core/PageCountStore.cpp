#include "PageCountStore.h"
#include "Book32FS.h"
#include <ArduinoJson.h>

// Only ever touched from AppReader on the main loop (unlike ProgressStore /
// SettingsStore, no web handler reads or writes this), so no mutex is needed.

static const char* PAGE_TOTALS_PATH = "/page_totals.json";

// Read capacity follows the file; write capacity follows the entry count.
// Same approach as ProgressStore, just smaller since an entry here is only a
// name and an int.
static size_t readCapacityFor(size_t fileSize) {
    size_t cap = fileSize * 2 + 256;
    if (cap > 16384) cap = 16384;
    return cap;
}

static size_t writeCapacityFor(size_t entries) {
    size_t cap = 128 + entries * 48;
    if (cap > 16384) cap = 16384;
    return cap;
}

PageCountStore& PageCountStore::getInstance() {
    static PageCountStore instance;
    return instance;
}

void PageCountStore::load() {
    if (_loaded) return;
    _loaded = true;

    if (!EbookFS.exists(PAGE_TOTALS_PATH)) return;
    File file = EbookFS.open(PAGE_TOTALS_PATH, "r");
    if (!file) return;

    DynamicJsonDocument doc(readCapacityFor(file.size()));
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) return;

    _fontSize = doc["fontSize"] | 0;
    _fontFamily = doc["fontFamily"] | -1;

    JsonObject totals = doc["totals"].as<JsonObject>();
    if (!totals.isNull()) {
        for (JsonPair pair : totals) {
            _totals[String(pair.key().c_str())] = pair.value() | 0;
        }
    }

    JsonObject checkpoints = doc["checkpoints"].as<JsonObject>();
    if (!checkpoints.isNull()) {
        for (JsonPair pair : checkpoints) {
            JsonObject entry = pair.value().as<JsonObject>();
            PageCountCheckpoint c;
            c.chapter = entry["chapter"] | 0;
            c.pagesSoFar = entry["pagesSoFar"] | 0;
            _checkpoints[String(pair.key().c_str())] = c;
        }
    }
}

bool PageCountStore::save() {
    DynamicJsonDocument doc(writeCapacityFor(_totals.size() + _checkpoints.size()));
    doc["fontSize"] = _fontSize;
    doc["fontFamily"] = _fontFamily;

    JsonObject totals = doc.createNestedObject("totals");
    for (const auto& kv : _totals) totals[kv.first] = kv.second;

    JsonObject checkpoints = doc.createNestedObject("checkpoints");
    for (const auto& kv : _checkpoints) {
        JsonObject entry = checkpoints.createNestedObject(kv.first);
        entry["chapter"] = kv.second.chapter;
        entry["pagesSoFar"] = kv.second.pagesSoFar;
    }

    if (doc.overflowed()) return false;

    File file = EbookFS.open(PAGE_TOTALS_PATH, FILE_WRITE);
    if (!file) return false;
    serializeJson(doc, file);
    file.close();
    return true;
}

// (fontSize, fontFamily) mismatching what's on disk means every stored total
// and checkpoint was measured at a font no longer in use, so the whole cache
// is stale — drop it and adopt the new signature.
void PageCountStore::resetIfFontChanged(int fontSize, int fontFamily) {
    if (_fontSize == fontSize && _fontFamily == fontFamily) return;
    _totals.clear();
    _checkpoints.clear();
    _fontSize = fontSize;
    _fontFamily = fontFamily;
}

int PageCountStore::get(const String& originalName, int fontSize, int fontFamily) {
    load();
    if (_fontSize != fontSize || _fontFamily != fontFamily) return 0;
    auto it = _totals.find(originalName);
    return it != _totals.end() ? it->second : 0;
}

void PageCountStore::set(const String& originalName, int fontSize, int fontFamily, int totalPages) {
    load();
    resetIfFontChanged(fontSize, fontFamily);
    _totals[originalName] = totalPages;
    _checkpoints.erase(originalName);
    save();
}

bool PageCountStore::getCheckpoint(const String& originalName, int fontSize, int fontFamily,
                                   PageCountCheckpoint& out) {
    load();
    if (_fontSize != fontSize || _fontFamily != fontFamily) return false;
    auto it = _checkpoints.find(originalName);
    if (it == _checkpoints.end()) return false;
    out = it->second;
    return true;
}

void PageCountStore::setCheckpoint(const String& originalName, int fontSize, int fontFamily,
                                   const PageCountCheckpoint& checkpoint) {
    load();
    resetIfFontChanged(fontSize, fontFamily);
    _checkpoints[originalName] = checkpoint;
    save();
}
