#include "BookTitleStore.h"
#include "Book32FS.h"
#include "JsonFileStore.h"
#include "JsonStoreLogic.h"
#include <ArduinoJson.h>

// load() e save() ficam sem guarda de propósito: são privados e só se chegam
// lá a partir de um método público que já detém o mutex (recursivo). Mesma
// convenção do ProgressStore e do PageCountStore.

static const char* BOOK_TITLES_PATH = "/book_titles.json";

// Capacidade de leitura conforme o ficheiro, de escrita conforme as entradas —
// como no PageCountStore, só que aqui uma entrada é nome + título, ambos
// texto, e por isso a estimativa por entrada é maior. A aritmética (e a
// saturação) está em jsonStoreCapacity, com teste de host.
static const size_t BOOK_TITLES_MAX_CAPACITY = 24576; // 24 KB

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

    DynamicJsonDocument doc(jsonStoreCapacity(512, 2, file.size(), BOOK_TITLES_MAX_CAPACITY));
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
    DynamicJsonDocument doc(jsonStoreCapacity(256, 288, _titles.size(), BOOK_TITLES_MAX_CAPACITY));
    for (const auto& kv : _titles)
        doc[kv.first] = kv.second;

    // Um documento que transbordou (ou uma escrita que ficou a meio) grava
    // metade da biblioteca por cima da outra metade: mais vale ficar com o
    // ficheiro anterior e voltar a ler os títulos em falta na próxima visita
    // à biblioteca. Ver JsonFileStore.h.
    return writeJsonAtomic(SystemFS, BOOK_TITLES_PATH, doc, "BookTitleStore");
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

    if (reconcileStoreKeys(_titles, presentOriginalNames)) save();
}
