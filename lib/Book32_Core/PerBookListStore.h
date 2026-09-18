#pragma once
// Book32 — o store "uma lista por livro", partilhado pelos quatro caches de
// capítulos: ChapterTocStore (títulos), ChapterNarrativeStore (bools),
// ChapterGuideTypeStore (tipos do <guide>) e ChapterLengthStore
// (comprimentos).
//
// Os quatro tinham o mesmo ficheiro escrito quatro vezes: map<nome do livro,
// vector<T>> em SystemFS, um JSON de {nome: [valores]}, load() preguiçoso,
// get/set/reconcile com o mesmo mutex recursivo e as mesmas estimativas de
// capacidade com constantes diferentes. ~480 linhas de cópia a cópia, e uma
// correcção (a escrita atómica, ver JsonFileStore.h) teria de ser feita
// quatro vezes e ficaria a diferir na quinta.
//
// Cada store mantém a sua própria classe e a sua própria API pública — o que
// muda é só o corpo, que passa a delegar aqui. Continuam a ser ficheiros
// separados em disco, deliberadamente: um formato já gravado em aparelhos
// reais não muda de forma em silêncio (ver o cabeçalho do
// ChapterNarrativeStore).
//
// Os parâmetros de capacidade são os que cada store já usava — nada aqui
// muda o perfil de memória de nenhum deles.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <map>
#include <vector>
#include "JsonFileStore.h"
#include "JsonStoreLogic.h"
#include "Lock.h"

template <typename T> class PerBookListStore {
  public:
    // `readSlack`/`writeBase`/`writePerEntry`/`maxCapacity` são as constantes
    // da estimativa de capacidade (ver jsonStoreCapacity): ler dimensiona o
    // documento pelo tamanho do ficheiro, gravar pelo número total de
    // entradas. `tag` só aparece em mensagens no monitor série.
    PerBookListStore(fs::FS& fs, const char* path, const char* tag, size_t maxCapacity, size_t readSlack,
                     size_t writeBase, size_t writePerEntry)
        : _fs(fs), _path(path), _tag(tag), _maxCapacity(maxCapacity), _readSlack(readSlack),
          _writeBase(writeBase), _writePerEntry(writePerEntry) {}

    // false quando o livro ainda não tem nada guardado. Uma lista vazia nunca
    // é gravada (ver set), por isso `out` vem sempre preenchido quando é true.
    bool get(const String& originalName, std::vector<T>& out) {
        Book32Guard guard(_mutex);
        load();
        auto it = _entries.find(originalName);
        if (it == _entries.end() || it->second.empty()) return false;
        out = it->second;
        return true;
    }

    // Grava a lista completa do livro de uma só vez: os quatro caches são
    // sempre construídos inteiros antes de chegarem aqui (ver BookIndexer),
    // por isso não há versões parciais a fundir. Uma lista vazia é ignorada.
    void set(const String& originalName, const std::vector<T>& values) {
        if (originalName.length() == 0 || values.empty()) return;
        Book32Guard guard(_mutex);
        load();
        auto it = _entries.find(originalName);
        if (it != _entries.end() && it->second == values) return; // nada mudou, não gastar uma escrita
        _entries[originalName] = values;
        save();
    }

    // Larga o que estiver guardado para livros cujo .epub já não existe,
    // chamado a partir do scanBooks().
    void reconcile(const std::vector<String>& presentOriginalNames) {
        Book32Guard guard(_mutex);
        load();
        if (reconcileStoreKeys(_entries, presentOriginalNames)) save();
    }

  private:
    void load() {
        if (_loaded) return;
        _loaded = true;

        if (!_fs.exists(_path)) return;
        File file = _fs.open(_path, FILE_READ);
        if (!file) return;

        DynamicJsonDocument doc(jsonStoreCapacity(_readSlack, 2, file.size(), _maxCapacity));
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        if (error || !doc.is<JsonObject>()) return;

        for (JsonPair pair : doc.as<JsonObject>()) {
            JsonArray arr = pair.value().as<JsonArray>();
            if (arr.isNull()) continue;
            std::vector<T> values;
            values.reserve(arr.size());
            for (JsonVariant v : arr)
                values.push_back(v.as<T>());
            if (values.empty()) continue;
            _entries[String(pair.key().c_str())] = values;
        }
    }

    bool save() {
        size_t totalEntries = 0;
        for (const auto& kv : _entries)
            totalEntries += kv.second.size();

        DynamicJsonDocument doc(jsonStoreCapacity(_writeBase, _writePerEntry, totalEntries, _maxCapacity));
        for (const auto& kv : _entries) {
            JsonArray arr = doc.createNestedArray(kv.first);
            // Cópia para uma variável de tipo T em vez de uma referência: em
            // std::vector<bool> o operator[] devolve um proxy, que o
            // ArduinoJson não sabe converter.
            for (size_t i = 0; i < kv.second.size(); ++i) {
                T value = kv.second[i];
                arr.add(value);
            }
        }

        // O transbordo, a escrita curta e o rename falhado ficam todos com o
        // ficheiro anterior em vez de gravarem metade da biblioteca por cima
        // da outra metade. Ver JsonFileStore.h.
        return writeJsonAtomic(_fs, _path, doc, _tag);
    }

    fs::FS& _fs;
    const char* _path;
    const char* _tag;
    size_t _maxCapacity;
    size_t _readSlack;
    size_t _writeBase;
    size_t _writePerEntry;

    // O leitor toca nisto a partir do loop principal e o servidor web a
    // partir da sua própria tarefa. Ver Lock.h.
    Book32Mutex _mutex;
    bool _loaded = false;
    std::map<String, std::vector<T>> _entries;
};
