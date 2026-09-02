#ifndef BOOK_INDEXER_H
#define BOOK_INDEXER_H

#include <Arduino.h>
#include <vector>
#include "EpubLoader.h"
#include "TextRenderer.h"

// M1/D6 — um só varrimento de capítulos por livro em vez de scanners
// independentes (contagem de páginas + índice de títulos), que liam e
// analisavam o mesmo capítulo duas vezes por abertura sem cache (ver
// docs/plans/2026-09-02-avaliacao-codigo-ereader.md). Cada capítulo é lido
// uma só vez com EpubLoader::getChapterContentRich(); o resultado alimenta
// o que ainda estiver por fazer das três coisas:
//   - contar páginas (needPageCount), com um TextRenderer próprio (draw=false)
//     para nunca perturbar a página que está no ecrã — cacheada por (livro,
//     tamanho, família) em PageCountStore;
//   - construir o índice de títulos (needToc), usando
//     EpubLoader::chapterTitleFromContent() sobre o mesmo conteúdo em vez de
//     reabrir o capítulo — cacheado em ChapterTocStore (e, ao terminar,
//     ChapterNarrativeStore/ChapterGuideTypeStore sem custo extra: o <guide>
//     já foi lido quando o EPUB abriu);
//   - medir o comprimento de texto de cada capítulo (needLengths), para um
//     "ir para %" pendente reaproveitar sem precisar de varrimento próprio —
//     cacheado em ChapterLengthStore.
//
// AppReader chama start() quando um livro abre (ou quando o tamanho/família
// de letra muda) e step(budgetMs) a partir de update(), em fatias, tal como
// os outros scanners de fundo. O "ir para %" (AppReader::_percentSeekActive)
// fica de fora deste fecho: é um pedido raro e pontual que já se resolve de
// imediato quando ChapterLengthStore tem tudo em cache (ver
// AppReader::startPercentSeek), e só faz o próprio varrimento — mais leve,
// sem TextRenderer nenhum — quando não tem; fundir esse caminho neste
// mecanismo é trabalho maior deixado para outra sessão.
class BookIndexer {
  public:
    BookIndexer();
    ~BookIndexer();

    // Verifica as três caches para `originalName` e prepara o varrimento do
    // que ainda faltar — isActive() fica false se estiver tudo já resolvido.
    // epubLoader tem de ficar vivo enquanto o varrimento estiver activo
    // (mesmo livro aberto, ver reset()); fontSizePt/fontFamily só afectam a
    // contagem de páginas (as outras duas caches não dependem de letra).
    void start(EpubLoader* epubLoader, const String& originalName, int fontSizePt, int fontFamily);

    // Avança o varrimento por uma fatia de até budgetMs. Não faz nada
    // enquanto !isActive().
    void step(unsigned long budgetMs);

    // Larga o varrimento a meio (livro fechado antes de terminar) sem
    // persistir mais do que os capítulos já completos já persistiram (ver
    // advanceChapter). Chamado por AppReader::closeBook().
    void reset();

    bool isActive() const {
        return _active;
    }
    // 0 até se saber — mesma semântica de sempre em PageCountStore.
    int totalPages() const {
        return _totalPages;
    }

    // Sums the character length of a chapter's already-parsed rich content —
    // text nodes directly, table cells cell by cell. No font measurement:
    // this is only ever used to compare chapters against each other for "go
    // to %", so it only needs to be proportionally right, not pixel-accurate.
    static long chapterTextLength(const std::vector<ContentNode>& content);

  private:
    bool _active;
    bool _needPageCount;
    bool _needToc;
    bool _needLengths;
    int _chapter; // próximo capítulo a ler, partilhado pelas três tarefas
    int _pagesSoFar;
    TextRenderer* _renderer;                  // só existe enquanto _needPageCount
    PagePointer _pointer;                     // posição de paginação dentro do capítulo actual
    std::vector<ContentNode> _chapterContent; // conteúdo do capítulo actual
    std::vector<String> _titles;              // só acumulado se _needToc
    std::vector<long> _lengths;               // só acumulado se _needLengths
    int _totalPages;

    EpubLoader* _epubLoader; // não é dono — o mesmo EpubLoader do AppReader
    String _originalName;
    int _fontSizePt;
    int _fontFamily;

    // Avança _chapter e, se a contagem de páginas ainda estiver activa,
    // grava um checkpoint em PageCountStore — só é lido de volta quando o
    // índice e os comprimentos já estiverem ambos resolvidos (ver start()),
    // por isso é seguro escrevê-lo sempre que a contagem avança, mesmo que
    // este varrimento também esteja a construir o índice.
    void advanceChapter();
    // Fecha o varrimento: persiste cada uma das três caches que ainda
    // estivesse por fazer e liberta o _renderer.
    void finish();
};

#endif
