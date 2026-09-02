#pragma once
// Book32 v1.18.0 — cache local do índice (título por capítulo) de cada livro.
//
// Porquê: construir o índice implica ler e analisar o HTML de todos os
// capítulos do EPUB (ver EpubLoader::getChapterTitle), o mesmo tipo de
// trabalho que a contagem total de páginas evita fazer a cada abertura do
// livro (ver BookIndexer). Uma vez construído, o
// índice não depende de tamanho de letra nem de família — ao contrário da
// contagem de páginas — por isso só precisa de ser calculado uma vez por
// livro, nunca mais enquanto o EPUB não mudar.
//
// Chaveado pelo nome original, como o BookTitleStore e o ProgressStore.
// Cache local, como o BookTitleStore e o PageCountStore: não entra no
// export/import de estado entre dispositivos, porque qualquer dispositivo a
// reconstrói sozinho a partir do próprio EPUB.
//
// Serve também a interface web (GET /api/toc, ver WebMgr.cpp): não há botão
// físico livre no dispositivo para um ecrã de "lista de capítulos" (ver
// docs/plans/2026-08-29-bookmarks-and-goto-percent-design.md), por isso é a
// web que mostra o índice e pede o salto — ver GoToChapterStore.

#include <Arduino.h>
#include <map>
#include <vector>
#include "Lock.h"

class ChapterTocStore {
  public:
    static ChapterTocStore& getInstance();

    // `out` fica com um título por capítulo, na mesma ordem da spine do EPUB
    // (uma entrada vazia = nenhum cabeçalho detectado nesse capítulo — o
    // chamador decide o texto de recurso, ex. "Capítulo N"). Devolve false
    // quando o livro ainda não tem índice construído.
    bool get(const String& originalName, std::vector<String>& out);

    // Grava o índice completo do livro de uma só vez — é sempre construído
    // inteiro em segundo plano antes de se gravar (ver
    // BookIndexer), por isso não há aqui uma versão parcial a
    // fundir. Um vector vazio é ignorado.
    void set(const String& originalName, const std::vector<String>& titles);

    // Larga o índice de livros cujo .epub já não existe, como o
    // BookTitleStore faz no mesmo ponto do scanBooks().
    void reconcile(const std::vector<String>& presentOriginalNames);

  private:
    ChapterTocStore() {}

    // Mesma convenção do BookTitleStore: leitor e servidor web tocam no
    // mesmo std::map a partir de tarefas diferentes.
    Book32Mutex _mutex;
    void load();
    bool save();

    bool _loaded = false;
    std::map<String, std::vector<String>> _toc;
};
