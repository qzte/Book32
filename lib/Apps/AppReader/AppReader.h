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

    BookEntry() : hasProgress(false), globalPage(1), totalPages(0), titleResolved(false) {}
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
    void resolveNextBookTitle();
    // Um repintar por lote de títulos em vez de um por título: cada refresh
    // e-ink custa quase um segundo, e a lista inteira costuma resolver-se em
    // poucas passagens.
    bool _titlesDirty = false;
    void drawLibrary();
    void updateLibraryScroll();
    void drawBookTile(Book32Display& display, int x, int y, int w, int h, bool selected);

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

    // Total page count, for the reading footer and the library list. Paginating
    // a whole book up front would stall opening a large one, so it's counted a
    // little at a time from update() instead, using a renderer of its own so it
    // never disturbs the page actually on screen. See startTotalPagesCounting().
    int _totalPages;      // 0 until known for the currently open book
    bool _countingActive;
    TextRenderer* _countRenderer;
    int _countChapter;
    std::vector<ContentNode> _countChapterContent;
    PagePointer _countPointer;
    int _countPagesSoFar;
    static const unsigned long TOTAL_PAGES_BUDGET_MS = 15;
    void startTotalPagesCounting();
    void updateTotalPagesCount();

    // v1.18.0: título por capítulo, reaproveitando a detecção de cabeçalhos
    // já feita pelo EpubLoader ao analisar cada capítulo (ver
    // EpubLoader::getChapterTitle). Construído uma vez por livro, em fatias
    // orçamentadas a partir de update() — mesmo padrão do "ir para %"
    // abaixo, só que mais simples (não depende de tamanho de letra, por
    // isso só precisa de correr uma vez por livro, nunca mais enquanto o
    // EPUB não mudar) — e cacheado em ChapterTocStore. Não há botão físico
    // livre para um ecrã de lista de capítulos no dispositivo (ver
    // docs/plans/2026-08-29-bookmarks-and-goto-percent-design.md), por isso
    // é a web UI que mostra o índice e pede o salto de capítulo — ver
    // GoToChapterStore e applyChapterJump().
    bool _tocBuildActive;
    int _tocBuildChapter;
    std::vector<String> _tocBuildTitles;
    void startTocBuild();
    void updateTocBuild();
    // Aplica um pedido de "ir para capítulo" vindo da web (GoToChapterStore).
    // Ao contrário do "ir para %", o capítulo já é exacto — não há nada para
    // resolver em segundo plano, salta-se logo que o livro abre.
    void applyChapterJump(int targetChapter);

    // v1.14.0: "go to %" requested from the web UI (see GoToPercentStore),
    // applied the next time this specific book is opened. Content-length
    // proportional (see GoToPercentLogic.h for why), not exact-page: chapters
    // are scanned once for their text length (no font measurement, no
    // rendering — much cheaper than the total-page count above), budgeted
    // across update() calls the same way so a big book doesn't stall the
    // book-open path. While active, input is ignored (see handleInput) so a
    // button press during the scan can't act on the position this is about
    // to replace.
    bool _percentSeekActive;
    int _percentSeekTargetPercent;
    std::vector<long> _percentSeekChapterLengths;
    void startPercentSeek(int percent);
    void updatePercentSeek();
    static long chapterTextLength(const std::vector<ContentNode>& content);

    // Dynamic Pagination
    std::vector<ContentNode> _currentRichContent;
    PagePointer _currentPagePointer;
    std::vector<PagePointer> _pageHistory; // Stores start of each page for current chapter
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
    void loadChapter(int chapterIndex);
    void nextPage();
    void prevPage();
    void nextChapter();
    void prevChapter();
    void drawReading();
};

#endif
