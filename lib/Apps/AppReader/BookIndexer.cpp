#include "BookIndexer.h"
#include "DisplayMgr.h"
#include "PageCountStore.h"
#include "ChapterTocStore.h"
#include "ChapterNarrativeStore.h"
#include "ChapterGuideTypeStore.h"
#include "ChapterLengthStore.h"

BookIndexer::BookIndexer()
    : _active(false), _needPageCount(false), _needToc(false), _needLengths(false), _chapter(0),
      _pagesSoFar(0), _renderer(nullptr), _pointer({0, 0}), _totalPages(0), _epubLoader(nullptr),
      _fontSizePt(9), _fontFamily(0) {}

BookIndexer::~BookIndexer() {
    if (_renderer) delete _renderer;
}

// Sums the character length of a chapter's already-parsed rich content —
// text nodes directly, table cells cell by cell. No font measurement: this
// is only ever used to compare chapters against each other for "go to %", so
// it only needs to be proportionally right, not pixel-accurate.
long BookIndexer::chapterTextLength(const std::vector<ContentNode>& content) {
    long total = 0;
    for (const ContentNode& node : content) {
        if (node.type == CONTENT_TEXT) {
            total += node.textNode.text.length();
        } else if (node.type == CONTENT_TABLE) {
            for (const TableRow& row : node.table.rows) {
                for (const TableCell& cell : row.cells)
                    total += cell.content.length();
            }
        }
    }
    return total;
}

void BookIndexer::reset() {
    _active = false;
    _chapterContent.clear();
    _titles.clear();
    _lengths.clear();
    if (_renderer) {
        delete _renderer;
        _renderer = nullptr;
    }
}

// Kicks off (or skips, if every cache already has what it needs) the
// unified chapter scan for `originalName`. Each of the three needs is
// checked against its own cache independently: a page-count cache hit
// (font-scoped) doesn't imply a TOC or length cache hit (book-scoped, no
// font), and vice versa.
void BookIndexer::start(EpubLoader* epubLoader, const String& originalName, int fontSizePt, int fontFamily) {
    _totalPages = 0;
    _active = false;
    _chapter = 0;
    _pagesSoFar = 0;
    _pointer = {0, 0};
    _chapterContent.clear();
    _titles.clear();
    _lengths.clear();
    if (_renderer) {
        delete _renderer;
        _renderer = nullptr;
    }

    _epubLoader = epubLoader;
    _originalName = originalName;
    _fontSizePt = fontSizePt;
    _fontFamily = fontFamily;

    if (!_epubLoader || _originalName.length() == 0) return;
    int totalChapters = _epubLoader->getChapterCount();

    int cachedPages = PageCountStore::getInstance().get(_originalName, _fontSizePt, _fontFamily);
    _needPageCount = (cachedPages <= 0);
    if (!_needPageCount) _totalPages = cachedPages;

    std::vector<String> cachedTitles;
    _needToc = !(ChapterTocStore::getInstance().get(_originalName, cachedTitles) &&
                 (int)cachedTitles.size() == totalChapters);

    std::vector<long> cachedLengths;
    _needLengths = !(ChapterLengthStore::getInstance().get(_originalName, cachedLengths) &&
                     (int)cachedLengths.size() == totalChapters);

    if (!_needPageCount && !_needToc && !_needLengths) return; // nada por fazer

    // Um checkpoint de PageCountStore só deixa saltar capítulos quando a
    // contagem de páginas é a ÚNICA coisa por fazer: o índice e os
    // comprimentos não têm checkpoint próprio (sempre recomeçam do capítulo
    // 0 — ver o resto deste ficheiro), por isso, se qualquer um dos dois
    // ainda precisar dos capítulos iniciais, o cursor partilhado tem de
    // começar em 0 mesmo que a contagem já tivesse ido mais longe numa
    // sessão anterior.
    if (_needPageCount && !_needToc && !_needLengths) {
        PageCountCheckpoint checkpoint;
        if (PageCountStore::getInstance().getCheckpoint(_originalName, _fontSizePt, _fontFamily,
                                                        checkpoint)) {
            _chapter = checkpoint.chapter;
            _pagesSoFar = checkpoint.pagesSoFar;
        }
    }

    _active = true;
}

// Advances the unified scan by a time-boxed slice. Each chapter is read
// once with getChapterContentRich() and fed to whichever of the three tasks
// still needs it; only the page-count task touches a TextRenderer (its own
// instance so it never disturbs the page actually on screen — same
// reasoning as the old, separate _countRenderer).
//
// D12: only the idle-sleep timeout (BatteryMgr::enterIdleSleep("idle_timeout"))
// goes straight to esp_deep_sleep_start() with no chance to save — KEY2
// long-press standby calls the current app's stop() (AppReader::closeBook(),
// which calls reset() above) first (see InputMgr::enterStandby()). Either
// way, this scan can still be interrupted with nothing beyond the last
// fully-completed chapter's checkpoint saved (see advanceChapter): only the
// page-count sub-task checkpoints, being the one that actually benefits from
// resuming mid-book instead of restarting, with a TextRenderer pass per
// chapter instead of just a parse.
void BookIndexer::step(unsigned long budgetMs) {
    if (!_active) return;
    if (!_epubLoader) {
        _active = false;
        return;
    }

    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();

    if (_needPageCount && !_renderer) {
        _renderer = new TextRenderer(display.width(), display.height(), _fontSizePt);
        _renderer->setFontFamily(_fontFamily);
    }

    int totalChapters = _epubLoader->getChapterCount();

    unsigned long budgetEnd = millis() + budgetMs;
    while (_active && millis() < budgetEnd) {
        if (_chapterContent.empty()) {
            if (_chapter >= totalChapters) {
                finish();
                return;
            }

            _chapterContent = _epubLoader->getChapterContentRich(_chapter);
            _pointer = {0, 0};

            if (_needToc) _titles.push_back(EpubLoader::chapterTitleFromContent(_chapterContent));
            if (_needLengths) _lengths.push_back(chapterTextLength(_chapterContent));

            if (_chapterContent.empty()) {
                advanceChapter(); // nada para paginar neste capítulo
                continue;
            }
            if (_needPageCount) _pagesSoFar++; // First page of this chapter begins
        }

        if (!_needPageCount) {
            // Nada mais precisa deste conteúdo — título/comprimento (se
            // pedidos) já foram capturados acima na mesma leitura.
            _chapterContent.clear();
            advanceChapter();
            continue;
        }

        RenderResult r = _renderer->renderRichPageDynamic(display, _chapterContent, _pointer.nodeIndex,
                                                          _pointer.charOffset, 0, 0, false);
        if (r.pageFull) {
            _pagesSoFar++;
            _pointer.nodeIndex = r.nextNodeIndex;
            _pointer.charOffset = r.nextCharOffset;
        } else {
            _chapterContent.clear();
            advanceChapter();
        }
    }
}

void BookIndexer::advanceChapter() {
    _chapter++;
    if (_needPageCount) {
        PageCountCheckpoint checkpoint;
        checkpoint.chapter = _chapter;
        checkpoint.pagesSoFar = _pagesSoFar;
        PageCountStore::getInstance().setCheckpoint(_originalName, _fontSizePt, _fontFamily, checkpoint);
    }
}

void BookIndexer::finish() {
    _active = false;

    if (_needPageCount) {
        int total = max(1, _pagesSoFar);
        _totalPages = total;
        PageCountStore::getInstance().set(_originalName, _fontSizePt, _fontFamily, total);
    }

    if (_needToc && !_titles.empty()) {
        ChapterTocStore::getInstance().set(_originalName, _titles);

        // O <guide> do OPF já foi lido inteiro quando o EPUB abriu
        // (EpubLoader::open -> parseOpf -> parseGuide): isto não reabre o
        // ZIP nem repete trabalho por capítulo, ao contrário do varrimento
        // acima — por isso persiste-se de uma vez, sem precisar de
        // orçamento por passagem.
        int totalChapters = (int)_titles.size();
        std::vector<bool> narrative;
        narrative.reserve(totalChapters);
        std::vector<String> guideTypes;
        guideTypes.reserve(totalChapters);
        for (int i = 0; i < totalChapters; i++) {
            narrative.push_back(_epubLoader->isChapterNarrative(i));
            guideTypes.push_back(_epubLoader->getChapterGuideType(i));
        }
        ChapterNarrativeStore::getInstance().set(_originalName, narrative);
        ChapterGuideTypeStore::getInstance().set(_originalName, guideTypes);
    }

    if (_needLengths && !_lengths.empty()) {
        ChapterLengthStore::getInstance().set(_originalName, _lengths);
    }

    if (_renderer) {
        delete _renderer;
        _renderer = nullptr;
    }
}
