#include "BookTitleStore.h"
#include "Book32FS.h"
#include <ArduinoJson.h>

// load() e save() ficam sem guarda de propósito: são privados e só se chegam
// lá a partir de um método público que já detém o mutex (recursivo). Mesma
// convenção do ProgressStore e do PageCountStore.

static const char* BOOK_TITLES_PATH = "/book_titles.json";

// Capacidade de leitura conforme o ficheiro, de escrita conforme as entradas —
// como no PageCountStore, só que aqui uma entrada é nome + título, ambos
// texto, e por isso a estimativa por entrada é maior.
static size_t titlesReadCapacityFor(size_t fileSize) {
    size_t cap = fileSize * 2 + 512;
    if (cap > 24576) cap = 24576;
    return cap;
}

static size_t titlesWriteCapacityFor(size_t entries) {
    size_t cap = 256 + entries * 288;
    if (cap > 24576) cap = 24576;
    return cap;
}

BookTitleStore& BookTitleStore::getInstance() {
    static BookTitleStore instance;
    return instance;
}

void BookTitleStore::load() {
    if (_loaded) return;
    _loaded = true;

    if (!SystemFS.exists(BOOK_TITLES_PATH)) return;
    File file = SystemFS.open(BOOK_TITLES_PATH, FILE_READ);
    if (!file) return;

    DynamicJsonDocument doc(titlesReadCapacityFor(file.size()));
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error || !doc.is<JsonObject>()) return;

    for (JsonPair pair : doc.as<JsonObject>()) {
        String title = pair.value().as<String>();
        if (title.length() == 0) continue;
        _titles[String(pair.key().c_str())] = title;
    }
}

bool BookTitleStore::save() {
    DynamicJsonDocument doc(titlesWriteCapacityFor(_titles.size()));
    for (const auto& kv : _titles)
        doc[kv.first] = kv.second;

    // Um documento que transbordou grava metade da biblioteca por cima da
    // outra metade: mais vale ficar com o ficheiro anterior e voltar a ler os
    // títulos em falta na próxima visita à biblioteca.
    if (doc.overflowed()) return false;

    File file = SystemFS.open(BOOK_TITLES_PATH, FILE_WRITE);
    if (!file) return false;
    serializeJson(doc, file);
    file.close();
    return true;
}

bool BookTitleStore::get(const String& originalName, String& out) {
    Book32Guard guard(_mutex);
    load();
    auto it = _titles.find(originalName);
    if (it == _titles.end() || it->second.length() == 0) return false;
    out = it->second;
    return true;
}

void BookTitleStore::set(const String& originalName, const String& title) {
    if (originalName.length() == 0 || title.length() == 0) return;
    Book32Guard guard(_mutex);
    load();
    auto it = _titles.find(originalName);
    if (it != _titles.end() && it->second == title) return; // nada mudou, não gastar uma escrita
    _titles[originalName] = title;
    save();
}

void BookTitleStore::loadAll(std::map<String, String>& out) {
    Book32Guard guard(_mutex);
    load();
    out = _titles;
}

void BookTitleStore::reconcile(const std::vector<String>& presentOriginalNames) {
    Book32Guard guard(_mutex);
    load();

    bool changed = false;
    for (auto it = _titles.begin(); it != _titles.end();) {
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
            it = _titles.erase(it);
            changed = true;
        }
    }
    if (changed) save();
}
