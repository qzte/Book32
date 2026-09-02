#ifndef APP_READER_H
#define APP_READER_H

#include "BaseApp.h"
#include "EpubLoader.h"
#include "TextRenderer.h"
#include "../../Book32_Core/InputMgr.h"
#include "../../Book32_Core/GoToPercentLogic.h"
#include <vector>
#include <map>

enum ReaderState {
    VIEW_LIBRARY,
    VIEW_READING
};

struct BookEntry {
    String path;         // Full path to file
    String title;        // Display title
    String originalName; // v1.8.0: key used by ProgressStore (original filename)
    bool hasProgress;    // v1.8.0: true when a saved position exists
    int globalPage;      // v1.8.0: saved page, shown in the library list
    int totalPages;      // Cached total page count; 0 when not yet known
    // v1.17.0: false enquanto `title` for só o nome do ficheiro (que o upload
    // corta aos 28 caracteres). O título a sério vem do <dc:title> do EPUB,
    // lido fora do desenho — ver AppReader::resolveNextBookTitle().
    bool titleResolved;
    // v1.19.0: false até se tentar extrair a capa deste livro (ver
    // AppReader::resolveNextBookCover) — independente de a tentativa ter
    // encontrado capa ou não, para não reabrir o ZIP de um livro sem capa a
    // cada visita à biblioteca.
    bool coverAttempted;
    // Só true quando a tentativa encontrou e descodificou uma capa a sério;
    // controla se drawLibrary() desenha o bitmap real ou o desenho genérico.
    bool hasCoverThumb;

    BookEntry()
        : hasProgress(false), globalPage(1), totalPages(0), titleResolved(false), coverAttempted(false),
          hasCoverThumb(false) {}
};

class AppReader : public App {
public:
    AppReader();
    virtual ~AppReader();

    // App Interface
    void start() override;
    void stop() override;
    void update() override; // Main loop: Input handling
    void draw() override;   // Display handling

    // Icon
    const uint8_t* getIconImage() override;
    const char* getName() override { return "eReader"; }

    // A página do leitor ocupa o ecrã todo: o indicador de bateria do sistema
    // aterrava por cima do texto. Na biblioteca não há conflito.
    bool allowsSystemStatusIndicator() override { return _state != VIEW_READING; }

    bool hasBootResume();
    void resumeSavedBookOnStart();
    void handleInput(InputAction action);
    void forceRedraw() override;

    // Apply a new reading font size (9/12/18pt) live. Safe to call from the
    // main loop; re-paginates the current page from the saved position.
    void applyFontSize(int pt) override;

    // Apply a new reading font family (see ReaderFontFamily) live. Safe to
    // call from the main loop; re-paginates the current page.
    void applyFontFamily(int family) override;

private:
    ReaderState _state;

    // Library
    std::vector<BookEntry> _books;
    int _selectedBookIndex;
    bool _booksScanned;
    bool _librarySelectionOnlyRedraw;
    bool _resumeSavedBookOnStart;
    int _previousBookIndex;
    // Index of the first book drawn in the list. The list only shows as many
    // items as fit on screen, so moving selection past the visible window
    // scrolls it (see updateLibraryScroll()).
    int _libraryScrollOffset;
    void scanBooks();
    // v1.17.0: lê o <dc:title> de um livro que ainda não tenha título em
    // cache e guarda-o (BookTitleStore). Um livro por chamada, a partir do
    // update() e só na biblioteca: abrir um ZIP demora o suficiente para se
    // notar, e nenhuma biblioteca precisa disto mais do que uma vez por livro.
    bool resolveNextBookTitle(); // true = abriu um ZIP nesta chamada (ver update())
    // Um repintar por lote de títulos em vez de um por título: cada refresh
    // e-ink custa quase um segundo, e a lista inteira costuma resolver-se em
    // poucas passagens.
    bool _titlesDirty = false;

    // v1.19.0: mesma forma do resolveNextBookTitle() acima, mas para a capa
    // — um livro por passagem do update(), na biblioteca, com o resultado
    // (bitmap 1bpp já do tamanho do item) escrito para /covers/<nome>.thumb
    // em EbookFS. Sem índice à parte: a presença/tamanho do próprio ficheiro
    // é o cache, ver BookEntry::coverAttempted/hasCoverThumb e
    // coverThumbPathFor() em AppReader.cpp.
    void resolveNextBookCover();
    bool _coversDirty = false;
    void drawLibrary();
    void updateLibraryScroll();
    void drawBookTile(Book32Display& display, int x, int y, int w, int h, bool selected);
    // Desenha a capa real já cacheada para este livro (ver resolveNextBookCover);
    // chamada só quando BookEntry::hasCoverThumb é true.
    void drawBookCoverThumb(Book32Display& display, const String& bookPath, int x, int y, int w, int h);

    // Settings
    int _refreshEveryNPages;
    int _pageTurnsSinceRefresh;
    int _fontSizePt;          // Reading body font size in points (9/12/18)
    int _fontFamily;          // Reading font family (see ReaderFontFamily)
    bool _readingFirstDraw;   // Forces a full refresh on the next reading draw
    void loadSettings();

    // Reading
    EpubLoader* _epubLoader;
    TextRenderer* _textRenderer;
    String _currentBookPath;
    int _currentChapter;
    int _globalPageNumber; // Runtime tracking of global page (1-indexed)
    bool _needsRedraw;

    // D6: um só varrimento de capítulos por livro em vez de dois
    // scanners independentes (contagem de páginas + índice de títulos), que
    // liam e analisavam o mesmo capítulo duas vezes por abertura sem cache
    // (ver D6 da avaliação de código do eReader,
    // docs/plans/2026-09-02-avaliacao-codigo-ereader.md). Cada capítulo é
    // lido uma só vez com EpubLoader::getChapterContentRich(); o resultado
    // alimenta o que ainda estiver por fazer das três coisas:
    //   - contar páginas (_indexNeedPageCount), com um TextRenderer próprio
    //     (_indexRenderer, draw=false) para nunca perturbar a página que está
    //     no ecrã — cacheada por (livro, tamanho, família) em PageCountStore;
    //   - construir o índice de títulos (_indexNeedToc), usando
    //     EpubLoader::chapterTitleFromContent() sobre o mesmo conteúdo em vez
    //     de reabrir o capítulo — cacheado em ChapterTocStore;
    //   - medir o comprimento de texto de cada capítulo (_indexNeedLengths),
    //     para um "ir para %" pendente reaproveitar sem precisar de
    //     varrimento próprio — cacheado em ChapterLengthStore.
    // Uma tarefa já resolvida por cache (ex.: reabrir o mesmo livro com o
    // índice já construído mas a contagem de páginas invalidada por um novo
    // tamanho de letra) não repete o trabalho que já tinha: só um capítulo
    // continua a ser lido, para o que ainda falta.
    int _totalPages; // 0 until known for the currently open book
    bool _indexingActive;
    bool _indexNeedPageCount;
    bool _indexNeedToc;
    bool _indexNeedLengths;
    int _indexChapter; // próximo capítulo a ler, partilhado pelas três tarefas
    int _indexPagesSoFar;
    TextRenderer* _indexRenderer;                  // só existe enquanto _indexNeedPageCount
    PagePointer _indexPointer;                     // posição de paginação dentro do capítulo actual
    std::vector<ContentNode> _indexChapterContent; // conteúdo do capítulo actual
    std::vector<String> _indexTitles;              // só acumulado se _indexNeedToc
    std::vector<long> _indexLengths;               // só acumulado se _indexNeedLengths
    static const unsigned long INDEX_BUDGET_MS = 15;
    void startIndexing();
    void updateIndexing();
    // Avança _indexChapter e, se a contagem de páginas ainda estiver activa,
    // grava um checkpoint em PageCountStore — só é lido de volta quando o
    // índice e os comprimentos já estiverem ambos resolvidos (ver
    // startIndexing()), por isso é seguro escrevê-lo sempre que a contagem
    // avança, mesmo que este varrimento também esteja a construir o índice.
    void advanceIndexChapter(const String& key);
    // Fecha o varrimento: persiste cada uma das três caches que ainda
    // estivesse por fazer e liberta o _indexRenderer.
    void finishIndexing(const String& key);
    // Aplica um pedido de "ir para capítulo" vindo da web (GoToChapterStore).
    // Ao contrário do "ir para %", o capítulo já é exacto — não há nada para
    // resolver em segundo plano, salta-se logo que o livro abre.
    void applyChapterJump(int targetChapter);

    // v1.14.0: "go to %" requested from the web UI (see GoToPercentStore),
    // applied the next time this specific book is opened. Content-length
    // proportional (see GoToPercentLogic.h for why), not exact-page.
    // D6: resolves instantly when ChapterLengthStore already has every
    // chapter's length cached from a previous updateIndexing() pass (see
    // above) — no scan needed at all. Otherwise falls back to the original
    // per-chapter scan, budgeted across update() calls the same way so a big
    // book doesn't stall the book-open path. While active, input is ignored
    // (see handleInput) so a button press during the scan can't act on the
    // position this is about to replace.
    bool _percentSeekActive;
    int _percentSeekTargetPercent;
    std::vector<long> _percentSeekChapterLengths;
    void startPercentSeek(int percent);
    void updatePercentSeek();
    // Ponto comum a ambos os caminhos de startPercentSeek()/updatePercentSeek()
    // (comprimentos já em cache vs. acabados de varrer): resolve o alvo e
    // aplica-o à posição de leitura.
    void resolvePercentSeekTarget(const std::vector<long>& chapterLengths);
    static long chapterTextLength(const std::vector<ContentNode>& content);

    // Dynamic Pagination
    std::vector<ContentNode> _currentRichContent;
    PagePointer _currentPagePointer;
    std::vector<PagePointer> _pageHistory; // Stores start of each page for current chapter
    // D12: drawReading() is the only writer (its draw=true render already
    // measures where this page ends as a side effect) and nextPage() is the
    // only reader — it reuses that measurement to decide whether to advance
    // instead of re-measuring the same page with a second draw=false pass,
    // and only does that pass itself when _currentPageRenderValid is false
    // (e.g. right after a jump, resize, or page turn invalidated it).
    RenderResult _currentPageRender;
    bool _currentPageRenderValid;

    bool openBook(const String& path, bool restoreProgress = true);
    bool openSavedProgress();
    // v1.8.0: keyed by original filename via ProgressStore, not by path.
    bool loadBookProgress(const String& originalName, int& chapter, PagePointer& pointer, int& globalPage);
    // Marca a posição actual como por gravar. A escrita em si acontece em
    // flushProgress(), chamado pelo update() quando o leitor fica parado e
    // sempre que o livro é fechado (o que inclui o standby, que passa por
    // stop()). Gravar a cada virar de página reescrevia o reader_progress.json
    // inteiro centenas de vezes por sessão de leitura, sempre nos mesmos
    // blocos do LittleFS.
    void saveReadingProgress(bool resumeOnBoot);
    void flushProgress();
    bool _progressDirty = false;
    bool _progressResumeOnBoot = false;
    unsigned long _lastProgressChangeMs = 0;
    static const unsigned long PROGRESS_FLUSH_DELAY_MS = 4000;

    void markProgressInactive();
    void closeBook(bool markInactive = true);
    // Tenta carregar chapterIndex, avançando por capítulos seguintes vazios
    // (páginas de navegação/créditos sem texto) até encontrar um com
    // conteúdo. Devolve false sem tocar em nenhum estado (capítulo, posição,
    // _currentRichContent, _pageHistory) quando NENHUM capítulo a partir daí
    // até ao fim do livro tem conteúdo — deixa a chamada decidir o que fazer
    // em vez de aterrar numa página em branco (ver nextPage(), D7).
    bool loadChapter(int chapterIndex);
    // D5: pagina `content` do início ({0,0}) com o _textRenderer da leitura
    // em uso (draw=false), devolvendo o ponteiro de início de cada página em
    // `outPages`, pela ordem em que aparecem — vazio quando `content` está
    // vazio. Síncrono, não orçamentado por update(): um capítulo típico
    // (10-30 páginas) pagina em poucas dezenas de ms, tolerável dentro de um
    // único virar de página ou reconstrução de posição, ao contrário dos
    // scanners de fundo acima (que existem só porque um livro inteiro não
    // cabe nesse orçamento). Usado para reconstruir _pageHistory ao retomar
    // um livro e para aterrar na ÚLTIMA página do capítulo anterior em vez
    // da primeira (ver prevChapter()) — a paginação "real" que nextPage()
    // faz com draw=false é a mesma medição, só que incremental.
    void paginateAll(const std::vector<ContentNode>& content, std::vector<PagePointer>& outPages);
    void nextPage();
    void prevPage();
    void nextChapter();
    void prevChapter();
    void drawReading();
};

#endif
