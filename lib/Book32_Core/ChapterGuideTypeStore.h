#pragma once
// Book32 — cache local do tipo de <guide><reference type="..."> (OPF2) de
// cada capítulo não-narrativo, ao lado do bool já guardado pelo
// ChapterNarrativeStore (ver EpubLoader::getChapterGuideType).
//
// Ficheiro separado do ChapterNarrativeStore, mesma razão que esse já usou
// para não entrar num campo a mais no ChapterTocStore: um formato já gravado
// em dispositivos reais (aqui, um array de bool) muda de forma em silêncio
// se se tentar meter lá uma string a mais — deserializeJson não erra, só
// devolve valores errados. Um ficheiro novo é puramente aditivo.
//
// Serve só para rotular no índice web (GET /api/toc) entradas não-narrativas
// sem título detectado (ex.: "Capa" em vez de "Capítulo N" genérico) — ver
// AppReader::updateTocBuild e data/script.js. Chaveado pelo nome original,
// cache local (não entra no export/import de estado entre dispositivos),
// mesmas convenções do ChapterNarrativeStore.

#include <Arduino.h>
#include <map>
#include <vector>
#include "Lock.h"

class ChapterGuideTypeStore {
  public:
    static ChapterGuideTypeStore& getInstance();

    // `out` fica com um tipo por capítulo (mesma ordem/tamanho do que
    // ChapterTocStore/ChapterNarrativeStore guardam para o mesmo livro,
    // "" onde o capítulo é narrativo ou não tem tipo de <guide>). Devolve
    // false quando o livro ainda não tem classificação guardada.
    bool get(const String& originalName, std::vector<String>& out);

    // Grava a lista completa de uma só vez, construída a partir de
    // EpubLoader::getChapterGuideType() (ver AppReader::updateTocBuild) — o
    // <guide> já foi lido quando o EPUB abriu, sem custo de parsing extra.
    // Um vector vazio é ignorado.
    void set(const String& originalName, const std::vector<String>& guideTypes);

    // Larga a classificação de livros cujo .epub já não existe, como o
    // ChapterNarrativeStore faz no mesmo ponto do scanBooks().
    void reconcile(const std::vector<String>& presentOriginalNames);

  private:
    ChapterGuideTypeStore() {}

    Book32Mutex _mutex;
    void load();
    bool save();

    bool _loaded = false;
    std::map<String, std::vector<String>> _guideTypes;
};
