#include "ChapterGuideTypeStore.h"
#include "Book32FS.h"
#include <ArduinoJson.h>

// load()/save() sem guarda de propósito: privados, só chegam lá a partir de
// um método público que já detém o mutex (recursivo). Mesma convenção do
// ChapterNarrativeStore.

static const char* CHAPTER_GUIDE_TYPE_PATH = "/chapter_guide_type.json";

// Tipos do <guide> são palavras curtas ("cover", "title-page", ...), bem
// mais leves que os títulos do ChapterTocStore, mas ainda strings — tecto a
// meio caminho entre o do ChapterNarrativeStore (bools) e o do
// ChapterTocStore.
static const size_t CHAPTER_GUIDE_TYPE_MAX_CAPACITY = 65536; // 64 KB

static size_t guideTypeReadCapacityFor(size_t fileSize) {
    size_t cap = fileSize * 2 + 512;
    if (cap > CHAPTER_GUIDE_TYPE_MAX_CAPACITY) cap = CHAPTER_GUIDE_TYPE_MAX_CAPACITY;
    return cap;
}

static size_t guideTypeWriteCapacityFor(size_t totalEntries) {
    size_t cap = 512 + totalEntries * 24;
    if (cap > CHAPTER_GUIDE_TYPE_MAX_CAPACITY) cap = CHAPTER_GUIDE_TYPE_MAX_CAPACITY;
    return cap;
}

ChapterGuideTypeStore& ChapterGuideTypeStore::getInstance() {
    static ChapterGuideTypeStore instance;
    return instance;
}

void ChapterGuideTypeStore::load() {
    if (_loaded) return;
    _loaded = true;

    if (!SystemFS.exists(CHAPTER_GUIDE_TYPE_PATH)) return;
    File file = SystemFS.open(CHAPTER_GUIDE_TYPE_PATH, FILE_READ);
    if (!file) return;

    DynamicJsonDocument doc(guideTypeReadCapacityFor(file.size()));
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error || !doc.is<JsonObject>()) return;

    for (JsonPair pair : doc.as<JsonObject>()) {
        JsonArray arr = pair.value().as<JsonArray>();
        if (arr.isNull()) continue;
        std::vector<String> types;
        types.reserve(arr.size());
        for (JsonVariant v : arr)
            types.push_back(v.as<String>());
        if (types.empty()) continue;
        _guideTypes[String(pair.key().c_str())] = types;
    }
}

bool ChapterGuideTypeStore::save() {
    size_t totalEntries = 0;
    for (const auto& kv : _guideTypes)
        totalEntries += kv.second.size();

    DynamicJsonDocument doc(guideTypeWriteCapacityFor(totalEntries));
    for (const auto& kv : _guideTypes) {
        JsonArray arr = doc.createNestedArray(kv.first);
        for (const String& type : kv.second)
            arr.add(type);
    }

    // Mesma regra do ChapterNarrativeStore::save(): um documento que
    // transbordou fica com o ficheiro anterior em vez de gravar metade da
    // biblioteca por cima da outra metade.
    if (doc.overflowed()) return false;

    File file = SystemFS.open(CHAPTER_GUIDE_TYPE_PATH, FILE_WRITE);
    if (!file) return false;
    serializeJson(doc, file);
    file.close();
    return true;
}

bool ChapterGuideTypeStore::get(const String& originalName, std::vector<String>& out) {
    Book32Guard guard(_mutex);
    load();
    auto it = _guideTypes.find(originalName);
    if (it == _guideTypes.end() || it->second.empty()) return false;
    out = it->second;
    return true;
}

void ChapterGuideTypeStore::set(const String& originalName, const std::vector<String>& guideTypes) {
    if (originalName.length() == 0 || guideTypes.empty()) return;
    Book32Guard guard(_mutex);
    load();
    auto it = _guideTypes.find(originalName);
    if (it != _guideTypes.end() && it->second == guideTypes) return; // nada mudou, não gastar uma escrita
    _guideTypes[originalName] = guideTypes;
    save();
}

void ChapterGuideTypeStore::reconcile(const std::vector<String>& presentOriginalNames) {
    Book32Guard guard(_mutex);
    load();

    bool changed = false;
    for (auto it = _guideTypes.begin(); it != _guideTypes.end();) {
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
            it = _guideTypes.erase(it);
            changed = true;
        }
    }
    if (changed) save();
}
