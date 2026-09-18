#include "PageCountStore.h"
#include "Book32FS.h"
#include "JsonFileStore.h"
#include "JsonStoreLogic.h"
#include <ArduinoJson.h>

// Todos os métodos públicos abrem com um Book32Guard; load(), save() e
// resetIfFontChanged() ficam sem guarda de propósito, porque são privados e só
// se chegam a partir de um método público que já detém o mutex (recursivo).
// Mesma convenção do ProgressStore.

static const char* PAGE_TOTALS_PATH = "/page_totals.json";

// Read capacity follows the file; write capacity follows the entry count.
// Same approach as ProgressStore, just smaller since an entry here is only a
// name and an int. The arithmetic itself lives in jsonStoreCapacity, which is
// host-tested (tools/tests/test_json_store.cpp).
static const size_t PAGE_TOTALS_MAX_CAPACITY = 16384; // 16 KB

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

    DynamicJsonDocument doc(jsonStoreCapacity(256, 2, file.size(), PAGE_TOTALS_MAX_CAPACITY));
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
    DynamicJsonDocument doc(
        jsonStoreCapacity(128, 48, _totals.size() + _checkpoints.size(), PAGE_TOTALS_MAX_CAPACITY));
    doc["fontSize"] = _fontSize;
    doc["fontFamily"] = _fontFamily;

    JsonObject totals = doc.createNestedObject("totals");
    for (const auto& kv : _totals)
        totals[kv.first] = kv.second;

    JsonObject checkpoints = doc.createNestedObject("checkpoints");
    for (const auto& kv : _checkpoints) {
        JsonObject entry = checkpoints.createNestedObject(kv.first);
        entry["chapter"] = kv.second.chapter;
        entry["pagesSoFar"] = kv.second.pagesSoFar;
    }

    // Fail-closed, like every other store: a refused write leaves the
    // previous totals in place instead of replacing them with a truncated
    // file that parses as nothing at all. See JsonFileStore.h.
    return writeJsonAtomic(EbookFS, PAGE_TOTALS_PATH, doc, "PageCountStore");
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
    Book32Guard guard(_mutex);
    load();
    if (_fontSize != fontSize || _fontFamily != fontFamily) return 0;
    auto it = _totals.find(originalName);
    return it != _totals.end() ? it->second : 0;
}

void PageCountStore::set(const String& originalName, int fontSize, int fontFamily, int totalPages) {
    Book32Guard guard(_mutex);
    load();
    resetIfFontChanged(fontSize, fontFamily);
    _totals[originalName] = totalPages;
    _checkpoints.erase(originalName);
    save();
}

bool PageCountStore::getCheckpoint(const String& originalName, int fontSize, int fontFamily,
                                   PageCountCheckpoint& out) {
    Book32Guard guard(_mutex);
    load();
    if (_fontSize != fontSize || _fontFamily != fontFamily) return false;
    auto it = _checkpoints.find(originalName);
    if (it == _checkpoints.end()) return false;
    out = it->second;
    return true;
}

void PageCountStore::setCheckpoint(const String& originalName, int fontSize, int fontFamily,
                                   const PageCountCheckpoint& checkpoint) {
    Book32Guard guard(_mutex);
    load();
    resetIfFontChanged(fontSize, fontFamily);
    _checkpoints[originalName] = checkpoint;
    save();
}
