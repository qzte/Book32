#include "ChapterLengthStore.h"
#include "Book32FS.h"
#include <ArduinoJson.h>

// load()/save() sem guarda de propósito: privados, só chegam lá a partir de
// um método público que já detém o mutex (recursivo). Mesma convenção do
// ChapterNarrativeStore/ChapterTocStore.

static const char* CHAPTER_LENGTH_PATH = "/chapter_length.json";

// Uma entrada por capítulo é um inteiro (comprimento em caracteres), mais
// pesado que o bit do ChapterNarrativeStore mas mais leve que as strings do
// ChapterTocStore.
static const size_t CHAPTER_LENGTH_MAX_CAPACITY = 65536; // 64 KB

static size_t lengthReadCapacityFor(size_t fileSize) {
    size_t cap = fileSize * 2 + 512;
    if (cap > CHAPTER_LENGTH_MAX_CAPACITY) cap = CHAPTER_LENGTH_MAX_CAPACITY;
    return cap;
}

static size_t lengthWriteCapacityFor(size_t totalEntries) {
    size_t cap = 512 + totalEntries * 16;
    if (cap > CHAPTER_LENGTH_MAX_CAPACITY) cap = CHAPTER_LENGTH_MAX_CAPACITY;
    return cap;
}

ChapterLengthStore& ChapterLengthStore::getInstance() {
    static ChapterLengthStore instance;
    return instance;
}

void ChapterLengthStore::load() {
    if (_loaded) return;
    _loaded = true;

    if (!SystemFS.exists(CHAPTER_LENGTH_PATH)) return;
    File file = SystemFS.open(CHAPTER_LENGTH_PATH, FILE_READ);
    if (!file) return;

    DynamicJsonDocument doc(lengthReadCapacityFor(file.size()));
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error || !doc.is<JsonObject>()) return;

    for (JsonPair pair : doc.as<JsonObject>()) {
        JsonArray arr = pair.value().as<JsonArray>();
        if (arr.isNull()) continue;
        std::vector<long> lengths;
        lengths.reserve(arr.size());
        for (JsonVariant v : arr)
            lengths.push_back(v.as<long>());
        if (lengths.empty()) continue;
        _lengths[String(pair.key().c_str())] = lengths;
    }
}

bool ChapterLengthStore::save() {
    size_t totalEntries = 0;
    for (const auto& kv : _lengths)
        totalEntries += kv.second.size();

    DynamicJsonDocument doc(lengthWriteCapacityFor(totalEntries));
    for (const auto& kv : _lengths) {
        JsonArray arr = doc.createNestedArray(kv.first);
        for (long v : kv.second)
            arr.add(v);
    }

    // Mesma regra do ChapterTocStore::save(): um documento que transbordou
    // fica com o ficheiro anterior em vez de gravar metade da biblioteca por
    // cima da outra metade.
    if (doc.overflowed()) return false;

    File file = SystemFS.open(CHAPTER_LENGTH_PATH, FILE_WRITE);
    if (!file) return false;
    serializeJson(doc, file);
    file.close();
    return true;
}

bool ChapterLengthStore::get(const String& originalName, std::vector<long>& out) {
    Book32Guard guard(_mutex);
    load();
    auto it = _lengths.find(originalName);
    if (it == _lengths.end() || it->second.empty()) return false;
    out = it->second;
    return true;
}

void ChapterLengthStore::set(const String& originalName, const std::vector<long>& lengths) {
    if (originalName.length() == 0 || lengths.empty()) return;
    Book32Guard guard(_mutex);
    load();
    auto it = _lengths.find(originalName);
    if (it != _lengths.end() && it->second == lengths) return; // nada mudou, não gastar uma escrita
    _lengths[originalName] = lengths;
    save();
}

void ChapterLengthStore::reconcile(const std::vector<String>& presentOriginalNames) {
    Book32Guard guard(_mutex);
    load();

    bool changed = false;
    for (auto it = _lengths.begin(); it != _lengths.end();) {
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
            it = _lengths.erase(it);
            changed = true;
        }
    }
    if (changed) save();
}
