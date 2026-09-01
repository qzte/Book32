#include "ChapterNarrativeStore.h"
#include "Book32FS.h"
#include <ArduinoJson.h>

// load()/save() sem guarda de propósito: privados, só chegam lá a partir de
// um método público que já detém o mutex (recursivo). Mesma convenção do
// ChapterTocStore.

static const char* CHAPTER_NARRATIVE_PATH = "/chapter_narrative.json";

// Uma entrada por capítulo é um único bit no JSON (0/1); bem mais leve que o
// tecto do ChapterTocStore, que guarda strings inteiras.
static const size_t CHAPTER_NARRATIVE_MAX_CAPACITY = 65536; // 64 KB

static size_t narrativeReadCapacityFor(size_t fileSize) {
    size_t cap = fileSize * 2 + 512;
    if (cap > CHAPTER_NARRATIVE_MAX_CAPACITY) cap = CHAPTER_NARRATIVE_MAX_CAPACITY;
    return cap;
}

static size_t narrativeWriteCapacityFor(size_t totalEntries) {
    size_t cap = 512 + totalEntries * 8;
    if (cap > CHAPTER_NARRATIVE_MAX_CAPACITY) cap = CHAPTER_NARRATIVE_MAX_CAPACITY;
    return cap;
}

ChapterNarrativeStore& ChapterNarrativeStore::getInstance() {
    static ChapterNarrativeStore instance;
    return instance;
}

void ChapterNarrativeStore::load() {
    if (_loaded) return;
    _loaded = true;

    if (!SystemFS.exists(CHAPTER_NARRATIVE_PATH)) return;
    File file = SystemFS.open(CHAPTER_NARRATIVE_PATH, FILE_READ);
    if (!file) return;

    DynamicJsonDocument doc(narrativeReadCapacityFor(file.size()));
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error || !doc.is<JsonObject>()) return;

    for (JsonPair pair : doc.as<JsonObject>()) {
        JsonArray arr = pair.value().as<JsonArray>();
        if (arr.isNull()) continue;
        std::vector<bool> flags;
        flags.reserve(arr.size());
        for (JsonVariant v : arr)
            flags.push_back(v.as<bool>());
        if (flags.empty()) continue;
        _narrative[String(pair.key().c_str())] = flags;
    }
}

bool ChapterNarrativeStore::save() {
    size_t totalEntries = 0;
    for (const auto& kv : _narrative)
        totalEntries += kv.second.size();

    DynamicJsonDocument doc(narrativeWriteCapacityFor(totalEntries));
    for (const auto& kv : _narrative) {
        JsonArray arr = doc.createNestedArray(kv.first);
        for (bool v : kv.second)
            arr.add(v);
    }

    // Mesma regra do ChapterTocStore::save(): um documento que transbordou
    // fica com o ficheiro anterior em vez de gravar metade da biblioteca por
    // cima da outra metade.
    if (doc.overflowed()) return false;

    File file = SystemFS.open(CHAPTER_NARRATIVE_PATH, FILE_WRITE);
    if (!file) return false;
    serializeJson(doc, file);
    file.close();
    return true;
}

bool ChapterNarrativeStore::get(const String& originalName, std::vector<bool>& out) {
    Book32Guard guard(_mutex);
    load();
    auto it = _narrative.find(originalName);
    if (it == _narrative.end() || it->second.empty()) return false;
    out = it->second;
    return true;
}

void ChapterNarrativeStore::set(const String& originalName, const std::vector<bool>& narrative) {
    if (originalName.length() == 0 || narrative.empty()) return;
    Book32Guard guard(_mutex);
    load();
    auto it = _narrative.find(originalName);
    if (it != _narrative.end() && it->second == narrative) return; // nada mudou, não gastar uma escrita
    _narrative[originalName] = narrative;
    save();
}

void ChapterNarrativeStore::reconcile(const std::vector<String>& presentOriginalNames) {
    Book32Guard guard(_mutex);
    load();

    bool changed = false;
    for (auto it = _narrative.begin(); it != _narrative.end();) {
        bool present = false;
        for (const auto& name : presentOriginalNames) {
            if (name == it->first) {
                present = true;
                break;
            }
        }
        if (present) {
            ++it;
        } else {
            it = _narrative.erase(it);
            changed = true;
        }
    }
    if (changed) save();
}
