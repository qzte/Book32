#include "ChapterTocStore.h"
#include "Book32FS.h"
#include <ArduinoJson.h>

// load() e save() ficam sem guarda de propósito: são privados e só se chegam
// lá a partir de um método público que já detém o mutex (recursivo). Mesma
// convenção do BookTitleStore.

static const char* CHAPTER_TOC_PATH = "/chapter_toc.json";

// Ao contrário do BookTitleStore (um título por livro), aqui cada livro tem
// um array inteiro de títulos, por isso o tecto tem de ser bem maior para
// caber uma biblioteca inteira já indexada.
static const size_t CHAPTER_TOC_MAX_CAPACITY = 131072; // 128 KB

static size_t tocReadCapacityFor(size_t fileSize) {
    size_t cap = fileSize * 2 + 1024;
    if (cap > CHAPTER_TOC_MAX_CAPACITY) cap = CHAPTER_TOC_MAX_CAPACITY;
    return cap;
}

static size_t tocWriteCapacityFor(size_t totalTitleEntries) {
    size_t cap = 1024 + totalTitleEntries * 96;
    if (cap > CHAPTER_TOC_MAX_CAPACITY) cap = CHAPTER_TOC_MAX_CAPACITY;
    return cap;
}

ChapterTocStore& ChapterTocStore::getInstance() {
    static ChapterTocStore instance;
    return instance;
}

void ChapterTocStore::load() {
    if (_loaded) return;
    _loaded = true;

    if (!SystemFS.exists(CHAPTER_TOC_PATH)) return;
    File file = SystemFS.open(CHAPTER_TOC_PATH, FILE_READ);
    if (!file) return;

    DynamicJsonDocument doc(tocReadCapacityFor(file.size()));
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error || !doc.is<JsonObject>()) return;

    for (JsonPair pair : doc.as<JsonObject>()) {
        JsonArray arr = pair.value().as<JsonArray>();
        if (arr.isNull()) continue;
        std::vector<String> titles;
        titles.reserve(arr.size());
        for (JsonVariant v : arr)
            titles.push_back(v.as<String>());
        if (titles.empty()) continue;
        _toc[String(pair.key().c_str())] = titles;
    }
}

bool ChapterTocStore::save() {
    size_t totalEntries = 0;
    for (const auto& kv : _toc)
        totalEntries += kv.second.size();

    DynamicJsonDocument doc(tocWriteCapacityFor(totalEntries));
    for (const auto& kv : _toc) {
        JsonArray arr = doc.createNestedArray(kv.first);
        for (const String& title : kv.second)
            arr.add(title);
    }

    // Um documento que transbordou grava metade da biblioteca por cima da
    // outra metade: mais vale ficar com o ficheiro anterior e o livro em
    // causa fica por indexar até se libertar espaço (mesma regra do
    // BookTitleStore::save()).
    if (doc.overflowed()) return false;

    File file = SystemFS.open(CHAPTER_TOC_PATH, FILE_WRITE);
    if (!file) return false;
    serializeJson(doc, file);
    file.close();
    return true;
}

bool ChapterTocStore::get(const String& originalName, std::vector<String>& out) {
    Book32Guard guard(_mutex);
    load();
    auto it = _toc.find(originalName);
    if (it == _toc.end() || it->second.empty()) return false;
    out = it->second;
    return true;
}

void ChapterTocStore::set(const String& originalName, const std::vector<String>& titles) {
    if (originalName.length() == 0 || titles.empty()) return;
    Book32Guard guard(_mutex);
    load();
    auto it = _toc.find(originalName);
    if (it != _toc.end() && it->second == titles) return; // nada mudou, não gastar uma escrita
    _toc[originalName] = titles;
    save();
}

void ChapterTocStore::reconcile(const std::vector<String>& presentOriginalNames) {
    Book32Guard guard(_mutex);
    load();

    bool changed = false;
    for (auto it = _toc.begin(); it != _toc.end();) {
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
            it = _toc.erase(it);
            changed = true;
        }
    }
    if (changed) save();
}
