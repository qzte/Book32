#pragma once
// Book32 — cache local de quais capítulos de cada livro são não-narrativos
// (capa, índice, página de rosto, créditos, dedicatória, etc.), classificados
// a partir do <guide> do OPF (ver EpubLoader::isChapterNarrative).
//
// Ficheiro separado do ChapterTocStore em vez de um campo a mais no mesmo
// JSON: o ChapterTocStore já tem um formato gravado em dispositivos reais
// (array de strings por livro); mudar isso para um array de objectos partia
// silenciosamente qualquer /chapter_toc.json existente até o livro ser
// reprocessado. Um ficheiro novo é puramente aditivo — mesma forma que
// GoToChapterStore espelha o GoToPercentStore em vez de reaproveitar o
// mesmo store por um campo extra.
//
// Chaveado pelo nome original, cache local (não entra no export/import de
// estado entre dispositivos), mesmas convenções do ChapterTocStore — ver
// esse ficheiro para o raciocínio completo.

#include <Arduino.h>
#include <map>
#include <vector>
#include "Lock.h"

class ChapterNarrativeStore {
  public:
    static ChapterNarrativeStore& getInstance();

    // `out` fica com um bool por capítulo (true = narrativo), na mesma ordem
    // da spine. Devolve false quando o livro ainda não tem classificação
    // guardada — o chamador trata tudo como narrativo nesse caso (mesmo
    // comportamento de sempre, antes desta funcionalidade existir).
    bool get(const String& originalName, std::vector<bool>& out);

    // Grava a classificação completa do livro de uma só vez, construída a
    // partir de EpubLoader::isChapterNarrative() (ver
    // BookIndexer) — sem custo de parsing adicional, o <guide>
    // já foi lido quando o EPUB abriu. Um vector vazio é ignorado.
    void set(const String& originalName, const std::vector<bool>& narrative);

    // Larga a classificação de livros cujo .epub já não existe, como o
    // ChapterTocStore faz no mesmo ponto do scanBooks().
    void reconcile(const std::vector<String>& presentOriginalNames);

  private:
    ChapterNarrativeStore() {}

    Book32Mutex _mutex;
    void load();
    bool save();

    bool _loaded = false;
    std::map<String, std::vector<bool>> _narrative;
};
