#include "AppReader.h"
#include "DisplayMgr.h"
#include "InputMgr.h"
#include "FontMgr.h"
#include "AppMgr.h"
#include "icon_reader.h"
#include "Book32FS.h"
#include "BookOrderLogic.h"
#include "BookMeta.h"
#include "ProgressStore.h"
#include "PageCountStore.h"
#include "BookmarkStore.h"
#include "BookTitleStore.h"
#include "BookTitleLogic.h"
#include "GoToPercentStore.h"
#include "ChapterTocStore.h"
#include "ChapterNarrativeStore.h"
#include "ChapterGuideTypeStore.h"
#include "ChapterLengthStore.h"
#include "GoToChapterStore.h"
#include "CoverImage.h"
#include "WebMgr.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
// Local FreeSans with Latin-1 Supplement (0x20-0xFF) so Portuguese book
// titles render correctly in the library list.
#include "Fonts/FreeSans.h"
#include <map>

static String normalizedBookName(const String& path) {
    String name = path;
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    slash = name.lastIndexOf('\\');
    if (slash >= 0) name = name.substring(slash + 1);
    return name;
}

// D5: allPages comes from AppReader::paginateAll() over the same content
// `target` is a position in. Fills outHistory with every page start
// strictly before target and returns true when target itself matches a
// real page boundary in that pagination; false (outHistory left empty)
// when it doesn't — e.g. a saved position from a different font/size that
// hasn't been re-paginated at this size yet, or a "go to %" node target
// that landed mid-page rather than exactly on one. Shared by openBook()'s
// resume path and AppReader::resolvePercentSeekTarget(), which both need
// "prev" right after landing to walk backward within the chapter instead of
// jumping straight to the previous one (see D5 in
// docs/plans/2026-09-02-avaliacao-codigo-ereader.md).
static bool splitPagesBefore(const std::vector<PagePointer>& allPages, const PagePointer& target,
                             std::vector<PagePointer>& outHistory) {
    outHistory.clear();
    for (const PagePointer& p : allPages) {
        if (p.nodeIndex > target.nodeIndex ||
            (p.nodeIndex == target.nodeIndex && p.charOffset >= target.charOffset)) {
            return true;
        }
        outHistory.push_back(p);
    }
    outHistory.clear();
    return false;
}

static int textWidthForFont(Book32Display& display, const char* text, const GFXfont* font) {
    int16_t x1, y1;
    uint16_t w, h;
    display.setFont(font);
    display.setTextSize(1);
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return w;
}

static void drawTextWithFont(Book32Display& display, const char* text, int x, int y, const GFXfont* font, uint16_t color) {
    display.setFont(font);
    display.setTextColor(color);
    display.setTextSize(1);
    display.setCursor(x, y);
    display.print(text);
}

// v1.19.0: tamanho fixo do item da biblioteca — capa real e desenho genérico
// partilham-no (ver drawLibrary, resolveNextBookCover, drawBookCoverThumb).
static const int BOOK32_COVER_WIDTH = 60;
static const int BOOK32_COVER_HEIGHT = 80;
static const size_t BOOK32_COVER_THUMB_BYTES =
    (size_t)((BOOK32_COVER_WIDTH + 7) / 8) * (size_t)BOOK32_COVER_HEIGHT;

// Deriva o caminho do bitmap de capa cacheado a partir do caminho do livro
// (ex.: "/Foo Bar.epub" -> "/covers/Foo Bar.thumb"). Extensão retirada pela
// posição do último ponto, como a limpeza em WebMgr.cpp já faz ao apagar um
// livro — mesma razão: String::replace(".epub") acertaria na primeira
// ocorrência em qualquer sítio do nome, não só no fim.
static String coverThumbPathFor(const String& bookPath) {
    String name = normalizedBookName(bookPath);
    int dot = name.lastIndexOf('.');
    String base = (dot > 0) ? name.substring(0, dot) : name;
    return "/covers/" + base + ".thumb";
}

static String titleFromFilename(String name) {
    name = normalizedBookName(name);
    int dot = name.lastIndexOf('.');
    if (dot > 0) name = name.substring(0, dot);
    name.replace('_', ' ');
    name.trim();
    return name;
}

struct LibraryDirtyRect {
    int x;
    int y;
    int w;
    int h;
};

static LibraryDirtyRect libraryItemRect(int index, int scrollOffset, int screenW) {
    const int HEADER_H = 76;
    const int BACK_ITEM_HEIGHT = 48;
    const int ITEM_HEIGHT = 110;
    if (index < 0) {
        return {14, HEADER_H, screenW - 28, BACK_ITEM_HEIGHT + 4};
    }
    int visibleRow = index - scrollOffset;
    return {14, HEADER_H + BACK_ITEM_HEIGHT + (visibleRow * ITEM_HEIGHT), screenW - 28, ITEM_HEIGHT + 4};
}

// How many book rows fit below the header/back item, given the same vertical
// budget the draw loop in drawLibrary() uses. Kept in sync with that loop's
// "if (y > display.height() - 70) break;" condition.
static int libraryItemsPerPage(int screenHeight) {
    const int HEADER_H = 76;
    const int BACK_ITEM_HEIGHT = 48;
    const int ITEM_HEIGHT = 110;
    int y = HEADER_H + BACK_ITEM_HEIGHT;
    int count = 0;
    while (y <= screenHeight - 70) {
        count++;
        y += ITEM_HEIGHT;
    }
    return count;
}

static LibraryDirtyRect unionLibraryRect(LibraryDirtyRect a, LibraryDirtyRect b) {
    int x1 = min(a.x, b.x);
    int y1 = min(a.y, b.y);
    int x2 = max(a.x + a.w, b.x + b.w);
    int y2 = max(a.y + a.h, b.y + b.h);
    return {x1, y1, x2 - x1, y2 - y1};
}

AppReader::AppReader() {
    _state = VIEW_LIBRARY;
    _selectedBookIndex = 0;
    _booksScanned = false;
    _librarySelectionOnlyRedraw = false;
    _resumeSavedBookOnStart = false;
    _previousBookIndex = 0;
    _libraryScrollOffset = 0;
    _epubLoader = nullptr;
    _textRenderer = nullptr;
    _currentChapter = 0;
    _needsRedraw = true;
    _totalPages = 0;
    _indexingActive = false;
    _indexNeedPageCount = false;
    _indexNeedToc = false;
    _indexNeedLengths = false;
    _indexRenderer = nullptr;
    _indexChapter = 0;
    _indexPointer = {0, 0};
    _indexPagesSoFar = 0;
    _percentSeekActive = false;
    _percentSeekTargetPercent = 0;
    _currentPageRender = {0, 0, false, 0, 0};
    _currentPageRenderValid = false;
    _pageTurnsSinceRefresh = 0;
    _refreshEveryNPages = 10; // Default to full refresh every 10 pages
    _fontSizePt = 9;          // Default body size (small)
    _fontFamily = READER_FONT_SANS; // Default family (system sans-serif)
    _readingFirstDraw = true;
    loadSettings();
}

void AppReader::loadSettings() {
    // Try EbookFS first (where uploadfs puts files), then SystemFS
    File file;
    if (EbookFS.exists("/reader_config.json")) {
        file = EbookFS.open("/reader_config.json", "r");
    } else if (SystemFS.exists("/reader_config.json")) {
        file = SystemFS.open("/reader_config.json", "r");
    }

    if (file) {
        DynamicJsonDocument doc(512);
        if (!deserializeJson(doc, file)) {
            if (doc.containsKey("refreshFrequency")) _refreshEveryNPages = doc["refreshFrequency"];
            if (doc.containsKey("fontSize")) {
                int pt = doc["fontSize"];
                _fontSizePt = (pt >= 18) ? 18 : (pt >= 12 ? 12 : 9);
            }
            if (doc.containsKey("fontFamily")) {
                int fam = doc["fontFamily"];
                _fontFamily =
                    (fam >= READER_FONT_SANS && fam <= READER_FONT_OPEN_SANS) ? fam : READER_FONT_SANS;
            }
        }
        file.close();
    }
    // No config file is fine - just use defaults
}

AppReader::~AppReader() {
    closeBook(false);
    if (_epubLoader) delete _epubLoader;
    if (_textRenderer) delete _textRenderer;
}

bool AppReader::hasBootResume() {
    ProgressStore& store = ProgressStore::getInstance();
    if (!store.resumeOnBoot()) return false;
    String last = store.lastBook();
    if (last.length() == 0) return false;
    return findFilenameForOriginal(last).length() > 0;
}

void AppReader::resumeSavedBookOnStart() {
    _resumeSavedBookOnStart = true;
}

void AppReader::start() {
    if (WiFi.getMode() != WIFI_OFF) {
        WebMgr::getInstance().stop();
        delay(50);
        WiFi.disconnect(false);
        WiFi.mode(WIFI_OFF);
        Serial.println("AppReader: WiFi powered down");
    }

    // Pick up any settings (font size, refresh interval) changed via the web UI
    // while we were away.
    loadSettings();
    if (_textRenderer) {
        _textRenderer->setFontSize(_fontSizePt);
        _textRenderer->setFontFamily(_fontFamily);
    }

    _state = VIEW_LIBRARY;
    _booksScanned = false;
    _librarySelectionOnlyRedraw = false;
    _needsRedraw = true;
    InputMgr::getInstance().setCallback(std::bind(&AppReader::handleInput, this, std::placeholders::_1));

    if (_resumeSavedBookOnStart) {
        _resumeSavedBookOnStart = false;
        if (!openSavedProgress()) {
            markProgressInactive();
        }
    }
}

void AppReader::stop() {
    closeBook();
    InputMgr::getInstance().clearCallback();
}

const uint8_t* AppReader::getIconImage() { return icon_reader_160x160; }

void AppReader::scanBooks() {
    _books.clear();
    std::map<String, String> metadata;
    loadBookMetadata(metadata);

    // v1.17.0: títulos já lidos de dentro dos EPUB. Os que faltarem ficam com
    // o nome do ficheiro e são resolvidos depois, um por passagem do update()
    // (ver resolveNextBookTitle) — abrir aqui o ZIP de cada livro punha o
    // caminho de entrar na biblioteca a depender do tamanho da biblioteca.
    std::map<String, String> epubTitles;
    BookTitleStore::getInstance().loadAll(epubTitles);

    File root = EbookFS.open("/");
    if(!root || !root.isDirectory()) return;
    File file = root.openNextFile();
    while(file){
        String fileName = normalizedBookName(file.name());
        String fileNameLower = fileName;
        fileNameLower.toLowerCase();
        if(fileNameLower.endsWith(".epub")) {
            BookEntry entry;
            entry.path = "/" + fileName;
            auto meta = metadata.find(fileName);
            // Convert to Latin-1 once at load time: the library list draws and
            // measures these bytes directly (bypassing FontMgr::drawText), and
            // the WebUI reads titles from books_meta.json, so UTF-8 is
            // preserved where it matters and collapsed where the display needs it.
            entry.originalName = (meta != metadata.end()) ? meta->second : fileName;
            auto cachedTitle = epubTitles.find(entry.originalName);
            entry.titleResolved = (cachedTitle != epubTitles.end());
            entry.title = FontMgr::utf8ToLatin1(entry.titleResolved ? cachedTitle->second
                                                                    : titleFromFilename(entry.originalName));

            // v1.19.0: ao contrário do título, isto é só um stat + tamanho do
            // ficheiro (sem abrir o ZIP), por isso faz-se aqui como
            // hasProgress/totalPages abaixo, não em resolveNextBookCover().
            String thumbPath = coverThumbPathFor(entry.path);
            if (EbookFS.exists(thumbPath)) {
                entry.coverAttempted = true;
                File coverFile = EbookFS.open(thumbPath, FILE_READ);
                if (coverFile) {
                    entry.hasCoverThumb = (coverFile.size() == BOOK32_COVER_THUMB_BYTES);
                    coverFile.close();
                }
            }

            _books.push_back(entry);
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();

    // v1.8.0: one read of the progress store for the whole library (not one
    // per book), plus pruning of entries whose .epub is gone and clearing of
    // `pending` for imported entries whose file has now arrived.
    {
        ProgressStore& store = ProgressStore::getInstance();
        std::vector<String> present;
        present.reserve(_books.size());
        for (const auto& b : _books) present.push_back(b.originalName);
        store.reconcile(present);
        // v1.14.0: same pruning rule for bookmarks — a bookmark for a book
        // that's no longer on flash is dead weight, same as a progress entry.
        BookmarkStore::getInstance().reconcile(present);
        // v1.17.0: e o mesmo para os títulos lidos dos EPUB.
        BookTitleStore::getInstance().reconcile(present);
        // v1.18.0: e para os índices de capítulo já construídos.
        ChapterTocStore::getInstance().reconcile(present);
        // e para a classificação narrativo/não-narrativo por capítulo.
        ChapterNarrativeStore::getInstance().reconcile(present);
        // e para o tipo de <guide> associado a cada capítulo não-narrativo.
        ChapterGuideTypeStore::getInstance().reconcile(present);
        // e para o comprimento de texto por capítulo (ver ChapterLengthStore).
        ChapterLengthStore::getInstance().reconcile(present);

        for (auto& b : _books) {
            BookProgress p;
            if (store.get(b.originalName, p)) {
                b.hasProgress = true;
                b.globalPage = p.globalPage;
            }
            // Only known once a book has been read all the way through at the
            // current font settings (see startIndexing) — scanning every
            // unread book here would be exactly the slow-open problem this
            // feature is careful to avoid.
            b.totalPages = PageCountStore::getInstance().get(b.originalName, _fontSizePt, _fontFamily);
        }
    }

    // v1.2.0: apply manual order from SystemFS /book_order.json.
    // Same merge rule as WebMgr's /api/books: ordered entries that still
    // exist first, remaining books appended in FS enumeration order.
    if (SystemFS.exists("/book_order.json")) {
        File of = SystemFS.open("/book_order.json", "r");
        if (of) {
            DynamicJsonDocument doc(4096);
            DeserializationError err = deserializeJson(doc, of);
            of.close();
            if (!err) {
                JsonArray arr = doc["order"].as<JsonArray>();
                if (!arr.isNull()) {
                    std::vector<String> order;
                    for (JsonVariant v : arr) order.push_back(v.as<String>());
                    applyBookOrderT(order, _books,
                        [](const BookEntry& e, const String& key) {
                            return e.path == "/" + key;
                        });
                }
            }
        }
    }
}

// v1.17.0: o nome do ficheiro em disco vem cortado aos 28 caracteres (é o
// tecto que o upload impõe, ver WebMgr), por isso derivar dele o título dava
// sempre uma linha só e cortada a meio da palavra. O título a sério está no
// <dc:title> do OPF, dentro do EPUB.
//
// Abrir o ZIP custa demasiado para o fazer no scanBooks() (que corre antes do
// primeiro desenho da biblioteca) e muito mais para o fazer dentro do ciclo de
// desenho, que repete a página. Fica aqui: um livro por passagem do update(),
// só na biblioteca, e o resultado vai para o BookTitleStore — a partir daí a
// lista já abre com os títulos certos.
// Devolve true quando abriu mesmo um ZIP nesta chamada (havia um título por
// resolver), false quando não havia nada a fazer — usado por update() para
// intercalar isto com resolveNextBookCover() em vez de arriscar as duas
// abrirem um ZIP na mesma passagem (ver ali).
bool AppReader::resolveNextBookTitle() {
    // O descritor do ZIP é global (ver zipFd em EpubLoader.cpp): abrir outro
    // livro com um aberto puxava-lhe o ficheiro debaixo dos pés. Na biblioteca
    // não há nenhum aberto, mas a guarda é barata e a invariante não é óbvia.
    if (_epubLoader) return false;

    int index = -1;
    for (size_t i = 0; i < _books.size(); i++) {
        if (!_books[i].titleResolved) {
            index = (int)i;
            break;
        }
    }

    if (index < 0) {
        // Lote terminado: um único repintar para todos os títulos que mudaram.
        if (_titlesDirty) {
            _titlesDirty = false;
            _librarySelectionOnlyRedraw = false;
            _needsRedraw = true;
        }
        return false;
    }

    BookEntry& book = _books[index];
    // Marcar antes de tentar: um EPUB sem <dc:title>, ou que não abre, não
    // pode ficar a ser reaberto a cada passagem do update().
    book.titleResolved = true;

    String fullPath = "/ebooks" + book.path;
    String rawTitle;
    EpubLoader loader;
    if (loader.open(fullPath.c_str())) {
        rawTitle = loader.getTitle();
        loader.close();
    }

    // O <dc:title> é texto de um ficheiro do utilizador: pode trazer marcação,
    // entidades XML e as mudanças de linha da indentação do OPF.
    String title = book32::sanitizeBookTitleT<String>(rawTitle);
    if (title.length() == 0) return true; // fica-se pelo nome do ficheiro, mas o ZIP abriu-se

    BookTitleStore::getInstance().set(book.originalName, title);

    // Latin-1 à entrada do desenho, como no scanBooks(): a lista mede e
    // desenha estes bytes directamente.
    String shown = FontMgr::utf8ToLatin1(title);
    if (shown != book.title) {
        book.title = shown;
        _titlesDirty = true;
    }
    return true;
}

// v1.19.0: mesma forma do resolveNextBookTitle() acima — um livro por
// passagem do update(), só na biblioteca, com a mesma guarda de
// exclusividade do ZIP (loadChapter/getChapterContentRich partilham o
// descritor global, ver zipFd em EpubLoader.cpp). Ao contrário dos títulos,
// não há um store à parte: o próprio ficheiro /covers/<nome>.thumb em
// EbookFS É o cache — presente e do tamanho certo = capa real; presente e
// vazio = "já tentado, sem capa"; ausente = por tentar (ver scanBooks(),
// que já preenche coverAttempted/hasCoverThumb com um simples stat).
void AppReader::resolveNextBookCover() {
    if (_epubLoader) return;

    int index = -1;
    for (size_t i = 0; i < _books.size(); i++) {
        if (!_books[i].coverAttempted) {
            index = (int)i;
            break;
        }
    }

    if (index < 0) {
        // Lote terminado: um único repintar, mesma razão do resolveNextBookTitle().
        if (_coversDirty) {
            _coversDirty = false;
            _librarySelectionOnlyRedraw = false;
            _needsRedraw = true;
        }
        return;
    }

    BookEntry& book = _books[index];
    book.coverAttempted = true; // marcar antes de tentar: sem capa não pode ficar a ser reaberto sempre

    if (!EbookFS.exists("/covers")) EbookFS.mkdir("/covers");
    String thumbPath = coverThumbPathFor(book.path);

    bool wroteCover = false;
    String fullPath = "/ebooks" + book.path;
    EpubLoader loader;
    if (loader.open(fullPath.c_str())) {
        if (loader.hasCoverImage()) {
            size_t jpegSize = 0;
            uint8_t* jpegData = loader.getCoverImageData(&jpegSize);
            if (jpegData) {
                if (jpegSize > 0) {
                    uint8_t bitmap[BOOK32_COVER_THUMB_BYTES];
                    // Só JPEG é suportado: um EPUB com capa PNG (ou outro
                    // formato) devolve false aqui e fica sem capa em vez de
                    // arriscar um segundo descodificador não verificável
                    // nesta sessão — ver CoverImage.h.
                    if (decodeJpegCoverToBitmap(jpegData, jpegSize, BOOK32_COVER_WIDTH, BOOK32_COVER_HEIGHT,
                                                bitmap)) {
                        File f = EbookFS.open(thumbPath, FILE_WRITE);
                        if (f) {
                            f.write(bitmap, sizeof(bitmap));
                            f.close();
                            wroteCover = true;
                        }
                    }
                }
                free(jpegData);
            }
        }
        loader.close();
    }

    if (!wroteCover) {
        // Marcador "sem capa" (ficheiro vazio): não voltar a tentar este livro.
        File f = EbookFS.open(thumbPath, FILE_WRITE);
        if (f) f.close();
    }

    book.hasCoverThumb = wroteCover;
    _coversDirty = true;
}

void AppReader::drawBookTile(Book32Display& display, int x, int y, int w, int h, bool selected) {
    display.fillRect(x, y, w, h, GxEPD_WHITE);
    display.drawRoundRect(x, y, w, h, 5, GxEPD_BLACK);
    display.drawRoundRect(x + 3, y + 3, w - 6, h - 6, 3, GxEPD_BLACK);
    display.fillRect(x + 6, y + 6, 5, h - 12, GxEPD_BLACK);

    int pageX = x + 17;
    int pageY = y + 14;
    int pageW = w - 27;
    display.drawFastHLine(pageX, pageY, pageW, GxEPD_BLACK);
    display.drawFastHLine(pageX, pageY + 12, pageW - 7, GxEPD_BLACK);
    display.drawFastHLine(pageX, pageY + 24, pageW, GxEPD_BLACK);
    display.drawFastHLine(pageX, pageY + 36, pageW - 11, GxEPD_BLACK);

    if (selected) {
        display.fillRect(x + w - 9, y + 8, 4, h - 16, GxEPD_BLACK);
    }
}

// Desenha a capa real já cacheada por resolveNextBookCover(). A moldura
// (fundo branco + contorno) é sempre desenhada primeiro, para que uma cache
// em falta ou de tamanho errado — livro apagado entre o scan e este desenho,
// ou chamado com um w/h diferente do cache — deixe uma caixa vazia em vez de
// nada ou de lixo no ecrã; o bitmap só é desenhado por cima se tudo bater certo.
void AppReader::drawBookCoverThumb(Book32Display& display, const String& bookPath, int x, int y, int w,
                                   int h) {
    display.fillRect(x, y, w, h, GxEPD_WHITE);
    display.drawRect(x, y, w, h, GxEPD_BLACK);
    if (w != BOOK32_COVER_WIDTH || h != BOOK32_COVER_HEIGHT) return;

    File f = EbookFS.open(coverThumbPathFor(bookPath), FILE_READ);
    if (!f) return;
    uint8_t buf[BOOK32_COVER_THUMB_BYTES];
    size_t n = f.read(buf, sizeof(buf));
    f.close();
    if (n != sizeof(buf)) return;

    display.drawBitmap(x, y, buf, BOOK32_COVER_WIDTH, BOOK32_COVER_HEIGHT, GxEPD_BLACK);
}

void AppReader::handleInput(InputAction action) {
    if (action == INPUT_NONE) return;
    Serial.printf("AppReader::handleInput - action: %d, state: %d\n", action, _state);
    if (_state == VIEW_LIBRARY) {
        // Index -1 = "Back to Menu", 0+ = books
        int maxIndex = (int)_books.size() - 1;
        if (action == INPUT_NEXT) {
            _previousBookIndex = _selectedBookIndex;
            _selectedBookIndex++;
            if (_selectedBookIndex > maxIndex) _selectedBookIndex = -1;  // Wrap to Back option
            _librarySelectionOnlyRedraw = _booksScanned;
            updateLibraryScroll();
            _needsRedraw = true;
        } else if (action == INPUT_PREV) {
            _previousBookIndex = _selectedBookIndex;
            _selectedBookIndex--;
            if (_selectedBookIndex < -1) _selectedBookIndex = maxIndex;  // Wrap to last book
            _librarySelectionOnlyRedraw = _booksScanned;
            updateLibraryScroll();
            _needsRedraw = true;
        } else if (action == INPUT_SELECT) {
            if (_selectedBookIndex == -1) {
                // Back to main menu
                markProgressInactive();
                AppMgr::getInstance().switchTo(0);
            } else if (!_books.empty() && _selectedBookIndex >= 0) {
                openBook(_books[_selectedBookIndex].path.c_str());
            }
        } else if (action == INPUT_BACK) {
            // KEY3: dedicated Back button. From the library, go straight to
            // the main menu without needing to navigate to the Back item.
            markProgressInactive();
            AppMgr::getInstance().switchTo(0);
        } else if (action == INPUT_GO_TO_MAIN_MENU) {
            // KEY1 long press: go to main menu from library
            Serial.println("AppReader: INPUT_GO_TO_MAIN_MENU -> switching to main menu");
            markProgressInactive();
            AppMgr::getInstance().switchTo(0);
        }
    } else if (_state == VIEW_READING) {
        // v1.14.0: a "go to %" jump is resolving in the background (see
        // updatePercentSeek) and about to replace the position on screen —
        // ignore input until it lands, so a page turn can't act on a
        // position that's seconds away from being overwritten.
        if (_percentSeekActive) return;
        if (action == INPUT_NEXT) nextPage();
        else if (action == INPUT_PREV) prevPage();
        else if (action == INPUT_SELECT) {
            closeBook();
            _state = VIEW_LIBRARY;
            // Force drawLibrary() to rescan: the book just closed may have
            // finished its total page count while it was open, and the list
            // scanned on the way in here is now stale for it.
            _booksScanned = false;
            _librarySelectionOnlyRedraw = false;
            _needsRedraw = true;
        }
        else if (action == INPUT_BACK) {
            // KEY3: dedicated Back button. Return to the library from the
            // reading view, same destination as INPUT_SELECT here.
            closeBook();
            _state = VIEW_LIBRARY;
            _booksScanned = false;
            _librarySelectionOnlyRedraw = false;
            _needsRedraw = true;
        } else if (action == INPUT_GO_TO_MAIN_MENU) {
            // KEY1 long press: go directly to main menu from reading view
            Serial.println("AppReader: INPUT_GO_TO_MAIN_MENU from READING -> switching to main menu");
            closeBook();
            _state = VIEW_LIBRARY;
            _booksScanned = false;
            _librarySelectionOnlyRedraw = false;
            _needsRedraw = true;
            markProgressInactive();
            AppMgr::getInstance().switchTo(0);
        }
    }
}

bool AppReader::openBook(const String& path, bool restoreProgress) {
    String fullPath = "/ebooks" + path;
    closeBook(false);
    _epubLoader = new EpubLoader();
    if (!_epubLoader->open(fullPath.c_str())) { delete _epubLoader; _epubLoader = nullptr; return false; }
    _currentBookPath = path;
    if (!_textRenderer) {
        DisplayMgr& dispMgr = DisplayMgr::getInstance();
        Book32Display& display = dispMgr.getDisplay();
        _textRenderer = new TextRenderer(display.width(), display.height(), _fontSizePt);
    }
    _textRenderer->setFontSize(_fontSizePt);      // Honor the current reading size
    _textRenderer->setFontFamily(_fontFamily);    // Honor the current reading font

    // Using Adafruit GFX bitmap fonts (same rendering path as main menu and all apps)
    Serial.println("TextRenderer: Using Adafruit GFX fonts");

    _textRenderer->calculateDimensions();

    // Paginating the whole book here would stall opening a large one, so only
    // the running position is known immediately; startIndexing() below fills
    // in the total a little at a time instead.
    _globalPageNumber = 1; // Start at page 1
    _currentPageRenderValid = false;
    
    int restoreChapter = 0;
    PagePointer restorePointer = {0, 0};
    int restorePage = 1;
    String progressKey = getOriginalFilename(normalizedBookName(path));
    bool restored = restoreProgress && loadBookProgress(progressKey, restoreChapter, restorePointer, restorePage);

    loadChapter(restored ? restoreChapter : 0);
    // The saved chapter can be gone (book replaced by a different edition), in
    // which case loadChapter fell through to a later one: restoring a pointer
    // from another chapter would land anywhere, so start that chapter clean.
    if (restored && restoreChapter != _currentChapter) {
        Serial.printf("AppReader: saved chapter %d unavailable, starting at %d\n",
                      restoreChapter, _currentChapter);
    }
    if (restored && restoreChapter == _currentChapter) {
        int maxNode = (int)_currentRichContent.size();
        if (restorePointer.nodeIndex >= 0 && restorePointer.nodeIndex <= maxNode && restorePointer.charOffset >= 0) {
            _currentPagePointer = restorePointer;
            _globalPageNumber = max(1, restorePage);
            _currentPageRenderValid = false;

            // D5: loadChapter() above left _pageHistory empty (a fresh
            // chapter load always does), so without this the first "prev"
            // after resuming a book would think we're at the very first
            // page of the chapter and jump to the previous chapter's first
            // page instead of going back one page within this one — the
            // single most frustrating "prev" behaviour for someone resuming
            // mid-chapter. Reconstructs the pages that come before the
            // restored position by actually paginating the chapter;
            // splitPagesBefore() leaves _pageHistory untouched (empty, same
            // as before this fix) if the position doesn't land on an exact
            // page boundary — e.g. a saved position from a font/size that
            // hasn't been re-paginated at this size yet.
            std::vector<PagePointer> allPages, history;
            paginateAll(_currentRichContent, allPages);
            if (splitPagesBefore(allPages, _currentPagePointer, history)) _pageHistory = history;
        }
    }

    _state = VIEW_READING;
    startIndexing();

    // v1.14.0: a "go to %" requested from the web UI while this book wasn't
    // open (see GoToPercentStore) applies now, overriding the position just
    // restored above. takePendingFor() only matches this exact book and
    // consumes the request, so it can't retrigger on a later re-open.
    int pendingPercent = 0;
    bool hasPendingSeek = GoToPercentStore::getInstance().takePendingFor(progressKey, pendingPercent);
    if (hasPendingSeek) {
        startPercentSeek(pendingPercent);
        // Don't draw the just-restored page: updatePercentSeek() draws once
        // the jump lands, so this avoids a visible flash of the wrong page
        // followed by a second full e-ink refresh a moment later.
        _needsRedraw = false;
    } else {
        // v1.18.0: same "web sets it, device applies it on open" shape for a
        // "go to chapter" request (GoToChapterStore, WebMgr's
        // /api/reader/goto-chapter) — only checked when no percent jump is
        // pending, so an unrelated chapter request left for a later open
        // isn't silently dropped here. Unlike the percent case this resolves
        // immediately: the index came straight from ChapterTocStore, so
        // there's nothing to scan for before landing on screen.
        int pendingChapter = -1;
        bool hasPendingChapterJump =
            GoToChapterStore::getInstance().takePendingFor(progressKey, pendingChapter);
        if (hasPendingChapterJump && pendingChapter >= 0 && pendingChapter < _epubLoader->getChapterCount()) {
            applyChapterJump(pendingChapter);
        }
        _needsRedraw = true;
    }

    // Abrir um livro grava já: é o que marca o livro como "último aberto" para
    // o resume no arranque, e acontece uma vez por livro, não por página.
    saveReadingProgress(true);
    flushProgress();
    return true;
}

bool AppReader::openSavedProgress() {
    ProgressStore& store = ProgressStore::getInstance();
    String last = store.lastBook();
    if (last.length() == 0) return false;

    // The stored key is the original (untruncated) name; map it back to the
    // file actually on flash.
    String filename = findFilenameForOriginal(last);
    if (filename.length() == 0) return false;

    return openBook("/" + filename, true);
}

bool AppReader::loadBookProgress(const String& originalName, int& chapter, PagePointer& pointer, int& globalPage) {
    BookProgress saved;
    if (!ProgressStore::getInstance().get(originalName, saved)) return false;

    chapter = saved.chapter;
    pointer.nodeIndex = saved.nodeIndex;
    pointer.charOffset = saved.charOffset;
    globalPage = saved.globalPage;
    return true;
}

void AppReader::saveReadingProgress(bool resumeOnBoot) {
    if (_currentBookPath.length() == 0 || _state != VIEW_READING) return;

    // Só marca; quem grava é flushProgress().
    _progressDirty = true;
    _progressResumeOnBoot = resumeOnBoot;
    _lastProgressChangeMs = millis();
}

void AppReader::flushProgress() {
    if (!_progressDirty) return;
    _progressDirty = false;

    if (_currentBookPath.length() == 0 || _state != VIEW_READING) return;

    String key = getOriginalFilename(normalizedBookName(_currentBookPath));
    if (key.length() == 0) return;

    BookProgress p;
    p.chapter = _currentChapter;
    p.nodeIndex = _currentPagePointer.nodeIndex;
    p.charOffset = _currentPagePointer.charOffset;
    p.globalPage = _globalPageNumber;

    ProgressStore& store = ProgressStore::getInstance();
    // _totalPages is 0 until the background count reaches the end of the book;
    // ProgressStore treats that as "cannot tell yet" and leaves the finish date
    // unset rather than guessing. See ProgressStore::set.
    store.set(key, p, _totalPages);
    store.setLast(key, _progressResumeOnBoot);
}

void AppReader::markProgressInactive() {
    ProgressStore::getInstance().setResumeOnBoot(false);
}

void AppReader::closeBook(bool markInactive) {
    if (markInactive && _state == VIEW_READING) {
        saveReadingProgress(false);
    }
    // Fechar o livro é o momento em que a posição diferida tem mesmo de ir
    // para o flash: a seguir o estado da página desaparece. Cobre também o
    // standby e o regresso ao menu, que passam por stop().
    flushProgress();
    if (_epubLoader) { _epubLoader->close(); delete _epubLoader; _epubLoader = nullptr; }
    if (_textRenderer) { delete _textRenderer; _textRenderer = nullptr; }
    _pageHistory.clear();
    _currentPageRenderValid = false;

    _indexingActive = false;
    _indexChapterContent.clear();
    _indexTitles.clear();
    _indexLengths.clear();
    if (_indexRenderer) {
        delete _indexRenderer;
        _indexRenderer = nullptr;
    }
}

// Kicks off (or skips, if every cache already has what it needs) the
// unified chapter scan for the book that just opened in
// _epubLoader/_currentBookPath — see the AppReader.h comment above
// _indexingActive for what it merges and why (D6). Each of the three needs
// is checked against its own cache independently: a page-count cache hit
// (font-scoped) doesn't imply a TOC or length cache hit (book-scoped, no
// font), and vice versa.
void AppReader::startIndexing() {
    _totalPages = 0;
    _indexingActive = false;
    _indexChapter = 0;
    _indexPagesSoFar = 0;
    _indexPointer = {0, 0};
    _indexChapterContent.clear();
    _indexTitles.clear();
    _indexLengths.clear();
    if (_indexRenderer) {
        delete _indexRenderer;
        _indexRenderer = nullptr;
    }

    if (!_epubLoader || _currentBookPath.length() == 0) return;
    String key = getOriginalFilename(normalizedBookName(_currentBookPath));
    int totalChapters = _epubLoader->getChapterCount();

    int cachedPages = PageCountStore::getInstance().get(key, _fontSizePt, _fontFamily);
    _indexNeedPageCount = (cachedPages <= 0);
    if (!_indexNeedPageCount) _totalPages = cachedPages;

    std::vector<String> cachedTitles;
    _indexNeedToc =
        !(ChapterTocStore::getInstance().get(key, cachedTitles) && (int)cachedTitles.size() == totalChapters);

    std::vector<long> cachedLengths;
    _indexNeedLengths = !(ChapterLengthStore::getInstance().get(key, cachedLengths) &&
                          (int)cachedLengths.size() == totalChapters);

    if (!_indexNeedPageCount && !_indexNeedToc && !_indexNeedLengths) return; // nada por fazer

    // Um checkpoint de PageCountStore só deixa saltar capítulos quando a
    // contagem de páginas é a ÚNICA coisa por fazer: o índice e os
    // comprimentos não têm checkpoint próprio (sempre recomeçam do capítulo
    // 0 — ver o resto deste ficheiro), por isso, se qualquer um dos dois
    // ainda precisar dos capítulos iniciais, o cursor partilhado tem de
    // começar em 0 mesmo que a contagem já tivesse ido mais longe numa
    // sessão anterior.
    if (_indexNeedPageCount && !_indexNeedToc && !_indexNeedLengths) {
        PageCountCheckpoint checkpoint;
        if (PageCountStore::getInstance().getCheckpoint(key, _fontSizePt, _fontFamily, checkpoint)) {
            _indexChapter = checkpoint.chapter;
            _indexPagesSoFar = checkpoint.pagesSoFar;
        }
    }

    _indexingActive = true;
}

// Advances the unified scan by a time-boxed slice. Each chapter is read
// once with getChapterContentRich() and fed to whichever of the three tasks
// still needs it; only the page-count task touches a TextRenderer
// (_indexRenderer, its own instance so it never disturbs the page actually
// on screen — same reasoning as the old _countRenderer).
//
// D12: only the idle-sleep timeout (BatteryMgr::enterIdleSleep("idle_timeout"))
// goes straight to esp_deep_sleep_start() with no chance to save — KEY2
// long-press standby calls the current app's stop() (closeBook() here)
// first (see InputMgr::enterStandby()). Either way, this scan can still be
// interrupted with nothing beyond the last fully-completed chapter's
// checkpoint saved (see advanceIndexChapter): only the page-count sub-task
// checkpoints, being the one that actually benefits from resuming mid-book
// instead of restarting, with a TextRenderer pass per chapter instead of
// just a parse.
void AppReader::updateIndexing() {
    if (!_epubLoader) {
        _indexingActive = false;
        return;
    }

    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();

    if (_indexNeedPageCount && !_indexRenderer) {
        _indexRenderer = new TextRenderer(display.width(), display.height(), _fontSizePt);
        _indexRenderer->setFontFamily(_fontFamily);
    }

    String key = getOriginalFilename(normalizedBookName(_currentBookPath));
    int totalChapters = _epubLoader->getChapterCount();

    unsigned long budgetEnd = millis() + INDEX_BUDGET_MS;
    while (_indexingActive && millis() < budgetEnd) {
        if (_indexChapterContent.empty()) {
            if (_indexChapter >= totalChapters) {
                finishIndexing(key);
                return;
            }

            _indexChapterContent = _epubLoader->getChapterContentRich(_indexChapter);
            _indexPointer = {0, 0};

            if (_indexNeedToc)
                _indexTitles.push_back(EpubLoader::chapterTitleFromContent(_indexChapterContent));
            if (_indexNeedLengths) _indexLengths.push_back(chapterTextLength(_indexChapterContent));

            if (_indexChapterContent.empty()) {
                advanceIndexChapter(key); // nada para paginar neste capítulo
                continue;
            }
            if (_indexNeedPageCount) _indexPagesSoFar++; // First page of this chapter begins
        }

        if (!_indexNeedPageCount) {
            // Nada mais precisa deste conteúdo — título/comprimento (se
            // pedidos) já foram capturados acima na mesma leitura.
            _indexChapterContent.clear();
            advanceIndexChapter(key);
            continue;
        }

        RenderResult r = _indexRenderer->renderRichPageDynamic(
            display, _indexChapterContent, _indexPointer.nodeIndex, _indexPointer.charOffset, 0, 0, false);
        if (r.pageFull) {
            _indexPagesSoFar++;
            _indexPointer.nodeIndex = r.nextNodeIndex;
            _indexPointer.charOffset = r.nextCharOffset;
        } else {
            _indexChapterContent.clear();
            advanceIndexChapter(key);
        }
    }
}

void AppReader::advanceIndexChapter(const String& key) {
    _indexChapter++;
    if (_indexNeedPageCount) {
        PageCountCheckpoint checkpoint;
        checkpoint.chapter = _indexChapter;
        checkpoint.pagesSoFar = _indexPagesSoFar;
        PageCountStore::getInstance().setCheckpoint(key, _fontSizePt, _fontFamily, checkpoint);
    }
}

void AppReader::finishIndexing(const String& key) {
    _indexingActive = false;

    if (_indexNeedPageCount) {
        int total = max(1, _indexPagesSoFar);
        _totalPages = total;
        PageCountStore::getInstance().set(key, _fontSizePt, _fontFamily, total);
    }

    if (_indexNeedToc && !_indexTitles.empty()) {
        ChapterTocStore::getInstance().set(key, _indexTitles);

        // O <guide> do OPF já foi lido inteiro quando o EPUB abriu
        // (EpubLoader::open -> parseOpf -> parseGuide): isto não reabre o
        // ZIP nem repete trabalho por capítulo, ao contrário do varrimento
        // acima — por isso persiste-se de uma vez, sem precisar de
        // orçamento por passagem.
        int totalChapters = (int)_indexTitles.size();
        std::vector<bool> narrative;
        narrative.reserve(totalChapters);
        std::vector<String> guideTypes;
        guideTypes.reserve(totalChapters);
        for (int i = 0; i < totalChapters; i++) {
            narrative.push_back(_epubLoader->isChapterNarrative(i));
            guideTypes.push_back(_epubLoader->getChapterGuideType(i));
        }
        ChapterNarrativeStore::getInstance().set(key, narrative);
        ChapterGuideTypeStore::getInstance().set(key, guideTypes);
    }

    if (_indexNeedLengths && !_indexLengths.empty()) {
        ChapterLengthStore::getInstance().set(key, _indexLengths);
    }

    if (_indexRenderer) {
        delete _indexRenderer;
        _indexRenderer = nullptr;
    }
}

// Applies a "go to chapter" request already resolved to an exact index (see
// GoToChapterStore) — no scanning needed, unlike updatePercentSeek(): the
// web UI already knows which chapter it wants, from the same ChapterTocStore
// list this build fills in. Same best-effort global-page approximation as
// updatePercentSeek's own landing spot: exact only once the total is cached,
// and proportional to the chapter's position in the book otherwise — this is
// still just a number in the footer, never used to resume from (the reader
// always resumes from chapter/nodeIndex/charOffset).
void AppReader::applyChapterJump(int targetChapter) {
    loadChapter(targetChapter);
    // loadChapter() can fall through to a later chapter if the target one
    // turned out empty (see its own fallback loop) — only place the pointer
    // and page number as if we landed where asked when we actually did.
    if (_currentChapter != targetChapter) return;

    _currentPagePointer = {0, 0};
    _currentPageRenderValid = false;

    int totalChapters = _epubLoader ? _epubLoader->getChapterCount() : 0;
    _globalPageNumber = (_totalPages > 0 && totalChapters > 0)
                            ? max(1, (int)(((long)_totalPages * (long)targetChapter) / totalChapters))
                            : 1;

    _readingFirstDraw = true; // full refresh: clears whatever was last on screen
    saveReadingProgress(true);
}

// Sums the character length of a chapter's already-parsed rich content —
// text nodes directly, table cells cell by cell. No font measurement: this
// is only ever used to compare chapters against each other for "go to %", so
// it only needs to be proportionally right, not pixel-accurate.
long AppReader::chapterTextLength(const std::vector<ContentNode>& content) {
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

// Kicks off a "go to %" resolution for the book currently open in
// _epubLoader. See updatePercentSeek() for the scanning path and
// resolvePercentSeekTarget() for how the result is carried out and applied.
void AppReader::startPercentSeek(int percent) {
    _percentSeekActive = false;
    _percentSeekChapterLengths.clear();
    if (!_epubLoader) return;
    int totalChapters = _epubLoader->getChapterCount();
    if (totalChapters <= 0) return; // nothing to seek into

    _percentSeekTargetPercent = percent;

    // D6: a previous unified chapter scan (see startIndexing/updateIndexing)
    // may already have every chapter's length cached — resolve the jump
    // right now instead of re-parsing the whole book just to measure it
    // again. Falls back to the scan below when it doesn't (this book was
    // never fully indexed, or the .epub changed and the cache no longer
    // matches its chapter count).
    String key = getOriginalFilename(normalizedBookName(_currentBookPath));
    std::vector<long> cachedLengths;
    if (ChapterLengthStore::getInstance().get(key, cachedLengths) &&
        (int)cachedLengths.size() == totalChapters) {
        resolvePercentSeekTarget(cachedLengths);
        return;
    }

    _percentSeekChapterLengths.reserve(totalChapters);
    _percentSeekActive = true;
}

// Advances the "go to %" scan by a time-boxed slice (same budget as the
// unified chapter scan: don't stall the main loop measuring a big book in
// one shot). Each tick measures one more chapter's text length with
// EpubLoader::getChapterContentRich() — parsing only, no TextRenderer, no
// pagination, so this is lighter than page counting and needs no renderer
// of its own. Only reached when startPercentSeek() couldn't resolve
// instantly from ChapterLengthStore.
void AppReader::updatePercentSeek() {
    if (!_epubLoader) {
        _percentSeekActive = false;
        return;
    }
    int totalChapters = _epubLoader->getChapterCount();

    unsigned long budgetEnd = millis() + INDEX_BUDGET_MS;
    while (_percentSeekActive && millis() < budgetEnd) {
        if ((int)_percentSeekChapterLengths.size() >= totalChapters) {
            _percentSeekActive = false;
            resolvePercentSeekTarget(_percentSeekChapterLengths);
            return;
        }

        int chapterIndex = (int)_percentSeekChapterLengths.size();
        std::vector<ContentNode> content = _epubLoader->getChapterContentRich(chapterIndex);
        _percentSeekChapterLengths.push_back(chapterTextLength(content));
    }
}

// resolvePercentTarget() (pure logic, see GoToPercentLogic.h) picks the
// chapter and in-chapter offset for the requested percent from
// chapterLengths, resolveNodeTarget() narrows that down to a content node,
// and the result replaces the live reading position — this is the one point
// where a "go to %" jump actually lands on screen, reached either straight
// from startPercentSeek() (cache hit) or once updatePercentSeek()'s scan
// finishes.
void AppReader::resolvePercentSeekTarget(const std::vector<long>& chapterLengths) {
    ChapterPercentTarget target = resolvePercentTarget(chapterLengths, _percentSeekTargetPercent);
    if (target.chapterIndex < 0) return; // no chapters — nothing to land on

    loadChapter(target.chapterIndex);
    // loadChapter() can fall through to a later chapter if the target one
    // turned out empty (see its own fallback loop) — only place the pointer
    // inside it if we actually landed where asked.
    if (_currentChapter == target.chapterIndex) {
        std::vector<int> nodeLengths;
        nodeLengths.reserve(_currentRichContent.size());
        for (const ContentNode& node : _currentRichContent) {
            nodeLengths.push_back(node.type == CONTENT_TEXT ? (int)node.textNode.text.length() : 0);
        }
        NodePositionTarget nodeTarget = resolveNodeTarget(nodeLengths, target.charOffsetInChapter);
        _currentPagePointer.nodeIndex = nodeTarget.nodeIndex;
        _currentPagePointer.charOffset = nodeTarget.charOffsetInNode;
        _currentPageRenderValid = false;

        // D5: same reconstruction as openBook()'s resume path — without it,
        // loadChapter() above left _pageHistory empty, so the first "prev"
        // after a "go to %" jump would think we're at the chapter's first
        // page and jump straight to the previous chapter instead of walking
        // back within this one.
        std::vector<PagePointer> allPages, history;
        paginateAll(_currentRichContent, allPages);
        if (splitPagesBefore(allPages, _currentPagePointer, history)) _pageHistory = history;

        // Best-effort page number: exact only when the total is already
        // cached at the current font settings (see PageCountStore); this is
        // the one number on screen that stays approximate otherwise, same
        // spirit as the "no made-up percentage" rule the library list
        // follows.
        _globalPageNumber = (_totalPages > 0)
                                ? max(1, (int)(((long)_totalPages * (long)_percentSeekTargetPercent) / 100))
                                : 1;
    }
    _readingFirstDraw = true; // full refresh: clears whatever was last on screen
    _needsRedraw = true;
    saveReadingProgress(true);
}

bool AppReader::loadChapter(int chapterIndex) {
    if (!_epubLoader) return false;
    if (chapterIndex < 0 || chapterIndex >= _epubLoader->getChapterCount()) return false;

    int totalChapters = _epubLoader->getChapterCount();
    for (int idx = chapterIndex; idx < totalChapters; idx++) {
        std::vector<ContentNode> content = _epubLoader->getChapterContentRich(idx);
        if (content.size() > 0) {
            _currentChapter = idx;
            _pageHistory.clear();
            _currentPagePointer = {0, 0};
            _currentPageRenderValid = false;
            _currentRichContent = content;
            if (_textRenderer) _textRenderer->clearCache();
            _needsRedraw = true;
            return true;
        }
    }
    // Every chapter from chapterIndex to the end of the book is empty
    // (trailing nav/credits/blank pages, common in commercial EPUB) — leave
    // the caller's previous chapter/page state exactly as it was instead of
    // clobbering it with an empty one; the caller decides what "no more
    // content" means for it (see nextPage(), D7).
    return false;
}

void AppReader::paginateAll(const std::vector<ContentNode>& content, std::vector<PagePointer>& outPages) {
    outPages.clear();
    if (!_textRenderer || content.empty()) return;

    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();

    PagePointer pointer = {0, 0};
    while (true) {
        outPages.push_back(pointer);
        // pageNum=0 with draw=false never touches _textRenderer's line
        // cache (see renderRichPageDynamic's `if (draw && ...)` cache-hit
        // guard) — this is a foreground, synchronous measurement in
        // response to a single button press, not a budgeted background
        // scan, so reusing the reader's own renderer instead of a separate
        // one (like the unified indexer's _indexRenderer) is fine: whatever
        // page we land on afterward gets a fresh draw=true render anyway.
        RenderResult r = _textRenderer->renderRichPageDynamic(display, content, pointer.nodeIndex,
                                                              pointer.charOffset, 0, 0, false);
        if (!r.pageFull) break; // reached the true end of this content
        pointer.nodeIndex = r.nextNodeIndex;
        pointer.charOffset = r.nextCharOffset;
    }
}

void AppReader::nextPage() {
    if (!_textRenderer) return;

    RenderResult result = _currentPageRender;
    if (!_currentPageRenderValid) {
        DisplayMgr& dispMgr = DisplayMgr::getInstance();
        Book32Display& display = dispMgr.getDisplay();
        int currentPageNum = _pageHistory.size();
        result = _textRenderer->renderRichPageDynamic(display, _currentRichContent,
                                                      _currentPagePointer.nodeIndex,
                                                      _currentPagePointer.charOffset,
                                                      currentPageNum, 0, false);
    }
    
    if (result.pageFull) {
        // Save current position to history before advancing
        _pageHistory.push_back(_currentPagePointer);
        
        // Continue from the exact node/character where rendering stopped.
        _currentPagePointer.nodeIndex = result.nextNodeIndex;
        _currentPagePointer.charOffset = result.nextCharOffset;
        
        // Increment global page counter
        _globalPageNumber++;
        
        // Clear cache since we're moving to a new page
        _textRenderer->clearCache();
        _currentPageRenderValid = false;
        saveReadingProgress(true);
        _needsRedraw = true;
    } else {
        // End of chapter - advance to next
        if (_currentChapter < _epubLoader->getChapterCount() - 1) {
            // loadChapter() clears _pageHistory itself once it lands on a
            // chapter with content (a fresh chapter starts with no saved
            // page positions) — nothing from the chapter we're leaving
            // belongs in it, so there's nothing to push here.
            if (loadChapter(_currentChapter + 1)) {
                _globalPageNumber++; // Next page in next chapter
                saveReadingProgress(true);
            }
            // Every remaining chapter is empty (trailing nav/credits pages):
            // loadChapter() left our state untouched, so just stay on this
            // last real page instead of looping through blank ones (D7).
        }
        // If at end of book, do nothing
    }
}

void AppReader::prevPage() {
    if (!_pageHistory.empty()) {
        _currentPagePointer = _pageHistory.back();
        _pageHistory.pop_back();
        if (_globalPageNumber > 1) _globalPageNumber--; // Decrement global page counter
        if (_textRenderer) _textRenderer->clearCache();
        _currentPageRenderValid = false;
        saveReadingProgress(true);
        _needsRedraw = true;
    } else {
        // Go to previous chapter — prevChapter() lands on its LAST page
        // (see D5), not its first.
        if (_currentChapter > 0) {
            if (_globalPageNumber > 1) _globalPageNumber--; // Decrement for prev chapter
            _currentPageRenderValid = false;
            prevChapter();
            saveReadingProgress(true);
        }
    }
}

void AppReader::nextChapter() {
    if (!_epubLoader) return;
    if (_currentChapter < _epubLoader->getChapterCount() - 1) loadChapter(_currentChapter + 1);
}

void AppReader::prevChapter() {
    if (!_epubLoader) return;
    if (_currentChapter > 0) {
        int tryChapter = _currentChapter - 1;
        while (tryChapter >= 0) {
            // Reuses the rich-content parser instead of the old plain-text
            // one: parseHtmlToRichContent() already drops empty text nodes
            // (EpubLoader.cpp), so an empty result here means the chapter
            // has no renderable content (nav/cover/divider pages) just as
            // reliably as the old length-of-stripped-text check did.
            std::vector<ContentNode> chapterContent = _epubLoader->getChapterContentRich(tryChapter);
            if (chapterContent.size() > 0) {
                loadChapter(tryChapter);
                // D5: land on this chapter's LAST page instead of its
                // first ("For now, we go to the start of the previous
                // chapter" — the single most frustrating "prev" behaviour
                // for someone rereading a paragraph). loadChapter() already
                // parsed this chapter into _currentRichContent, so reuse it
                // instead of a third parse.
                std::vector<PagePointer> allPages;
                paginateAll(_currentRichContent, allPages);
                if (!allPages.empty()) {
                    _currentPagePointer = allPages.back();
                    _pageHistory.assign(allPages.begin(), allPages.end() - 1);
                    _currentPageRenderValid = false;
                }
                return;
            }
            tryChapter--;
        }
    }
}

void AppReader::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;
    if (_state == VIEW_LIBRARY) drawLibrary();
    else drawReading();
}

// Keeps the selected book row inside the visible window, scrolling the list
// when the selection moves past its top or bottom edge. The "Back to Menu"
// row (-1) sits above the scrolling area and is always visible, so it leaves
// the current window untouched.
void AppReader::updateLibraryScroll() {
    if (_selectedBookIndex < 0) return;

    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();
    int itemsPerPage = libraryItemsPerPage(display.height());
    if (itemsPerPage <= 0) return;

    if (_selectedBookIndex < _libraryScrollOffset) {
        _libraryScrollOffset = _selectedBookIndex;
        _librarySelectionOnlyRedraw = false;  // Window shifted: repaint the whole list
    } else if (_selectedBookIndex >= _libraryScrollOffset + itemsPerPage) {
        _libraryScrollOffset = _selectedBookIndex - itemsPerPage + 1;
        _librarySelectionOnlyRedraw = false;
    }
}

void AppReader::drawLibrary() {
    if (!_booksScanned) { scanBooks(); _booksScanned = true; }
    // The book count can shrink between scans (book deleted via web UI while
    // the reader was open); keep the scroll window from pointing past the end.
    int maxOffset = max(0, (int)_books.size() - 1);
    if (_libraryScrollOffset > maxOffset) _libraryScrollOffset = 0;
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();

    const int HEADER_H = 76;
    const int BACK_ITEM_HEIGHT = 48;
    const int COVER_WIDTH = BOOK32_COVER_WIDTH;
    const int COVER_HEIGHT = BOOK32_COVER_HEIGHT;
    const int ITEM_HEIGHT = 110;
    const int ITEM_PADDING = 24;

    // Use Partial Refresh for Library interactions
    if (_librarySelectionOnlyRedraw) {
        LibraryDirtyRect dirty = unionLibraryRect(libraryItemRect(_previousBookIndex, _libraryScrollOffset, display.width()),
                                                 libraryItemRect(_selectedBookIndex, _libraryScrollOffset, display.width()));
        LibraryDirtyRect footer = {18, display.height() - 48, display.width() - 36, 46};
        dirty = unionLibraryRect(dirty, footer);
        dirty.x = max(0, dirty.x);
        dirty.y = max(0, dirty.y);
        if (dirty.x + dirty.w > display.width()) dirty.w = display.width() - dirty.x;
        if (dirty.y + dirty.h > display.height()) dirty.h = display.height() - dirty.y;
        display.setPartialWindow(dirty.x, dirty.y, dirty.w, dirty.h);
    } else {
        display.setPartialWindow(0, 0, display.width(), display.height());
    }
    _librarySelectionOnlyRedraw = false;

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        drawTextWithFont(display, "Library", 20, 40, &FreeSansBold12pt8b, GxEPD_BLACK);
        char countText[24];
        snprintf(countText, sizeof(countText), "%d books", (int)_books.size());
        fontMgr.drawTextRight(display, countText, display.width() - 20, 38, FONT_SIZE_SMALL, GxEPD_BLACK);
        display.drawFastHLine(20, 56, display.width() - 40, GxEPD_BLACK);
        display.drawFastHLine(20, 58, 72, GxEPD_BLACK);

        int y = HEADER_H;

        // === "Back to Menu" option (index -1) ===
        bool backSelected = (_selectedBookIndex == -1);
        if (backSelected) {
            display.fillRect(20, y + 6, 5, BACK_ITEM_HEIGHT - 12, GxEPD_BLACK);
            display.drawRoundRect(16, y + 2, display.width() - 32, BACK_ITEM_HEIGHT - 4, 6, GxEPD_BLACK);
        }
        drawTextWithFont(display, "<  Back to Menu", ITEM_PADDING + 14, y + 32,
                         backSelected ? &FreeSansBold12pt8b : &FreeSans12pt8b, GxEPD_BLACK);
        display.drawFastHLine(ITEM_PADDING, y + BACK_ITEM_HEIGHT - 1, display.width() - (ITEM_PADDING * 2), GxEPD_BLACK);
        y += BACK_ITEM_HEIGHT;

        // === Book list ===
        if (_books.empty()) {
            drawTextWithFont(display, "No books found.", 28, y + 54, &FreeSansBold12pt8b, GxEPD_BLACK);
            fontMgr.drawText(display, "Upload EPUBs via web.", 28, y + 88, FONT_SIZE_BODY, GxEPD_BLACK);
        } else {
            for (size_t idx = (size_t)_libraryScrollOffset; idx < _books.size(); idx++) {
                if (y > display.height() - 70) break;

                const auto& book = _books[idx];
                bool isSelected = ((int)idx == _selectedBookIndex);
                if (isSelected) {
                    display.fillRect(20, y + 12, 5, ITEM_HEIGHT - 24, GxEPD_BLACK);
                    display.drawRoundRect(16, y + 4, display.width() - 32, ITEM_HEIGHT - 8, 6, GxEPD_BLACK);
                } else {
                    display.drawFastHLine(ITEM_PADDING, y + ITEM_HEIGHT - 1, display.width() - (ITEM_PADDING * 2), GxEPD_BLACK);
                }

                int coverW = COVER_WIDTH;
                int coverH = COVER_HEIGHT;
                int coverX = ITEM_PADDING + 12;
                int coverY = y + (ITEM_HEIGHT - coverH) / 2;
                if (book.hasCoverThumb) {
                    drawBookCoverThumb(display, book.path, coverX, coverY, coverW, coverH);
                } else {
                    drawBookTile(display, coverX, coverY, coverW, coverH, isSelected);
                }
                if (isSelected) {
                    display.drawRect(coverX - 3, coverY - 3, coverW + 6, coverH + 6, GxEPD_BLACK);
                    display.drawRect(coverX - 2, coverY - 2, coverW + 4, coverH + 4, GxEPD_BLACK);
                }

                uint16_t textColor = GxEPD_BLACK;

                // Título em duas linhas, seleccionado ou não. Duas por três
                // razões: a terceira linha do item seleccionado (base em y+90)
                // aterrava por cima do "pag. x/y" (base em y+88); duas linhas
                // dão altura igual a todos os itens, o que mantém as caixas da
                // lista alinhadas; e chegam para os títulos reais dos EPUB.
                const GFXfont* titleFont = isSelected ? &FreeSansBold12pt8b : &FreeSans12pt8b;
                const int textX = ITEM_PADDING + COVER_WIDTH + 44;
                const int TITLE_MAX_LINES = 2;
                const int LINE_HEIGHT = 26;
                const int MAX_WIDTH = display.width() - textX - 28;

                // A quebra vive no BookTitleLogic.h (testada em host): mede com
                // a fonte do item, parte palavras que não caibam sozinhas e só
                // põe reticências quando sobra mesmo texto — e encolhe a linha
                // até as reticências caberem, em vez de tirar três caracteres à
                // sorte e voltar a transbordar.
                std::vector<String> titleLines = book32::wrapBookTitleT<String>(
                    book.title, TITLE_MAX_LINES, MAX_WIDTH, [&display, titleFont](const String& text) {
                        return textWidthForFont(display, text.c_str(), titleFont);
                    });

                int textY = y + 34;
                for (const String& line : titleLines) {
                    drawTextWithFont(display, line.c_str(), textX, textY, titleFont, textColor);
                    textY += LINE_HEIGHT;
                }

                // v1.8.0: saved position. No percentage: paginating the whole
                // book is too slow to do on open, so a percentage would be
                // made up.
                if (book.hasProgress) {
                    char pageLabel[32];
                    if (book.totalPages > 0) {
                        snprintf(pageLabel, sizeof(pageLabel), "pag. %d/%d", book.globalPage, book.totalPages);
                    } else {
                        snprintf(pageLabel, sizeof(pageLabel), "pag. %d", book.globalPage);
                    }
                    drawTextWithFont(display, pageLabel, textX, y + ITEM_HEIGHT - 22,
                                     &FreeSans9pt8b, textColor);
                }

                y += ITEM_HEIGHT;
            }
        }

        // Page indicator (14px) - show current selection
        char pageStr[24];
        if (_selectedBookIndex == -1) {
            snprintf(pageStr, sizeof(pageStr), "Menu");
        } else {
            snprintf(pageStr, sizeof(pageStr), "%d/%d", _selectedBookIndex + 1, (int)_books.size());
        }
        display.drawFastHLine(20, display.height() - 42, display.width() - 40, GxEPD_BLACK);
        fontMgr.drawText(display, "Next: Move  |  Hold: Open", 22, display.height() - 18, FONT_SIZE_SMALL, GxEPD_BLACK);
        fontMgr.drawTextRight(display, pageStr, display.width() - 20, display.height() - 18, FONT_SIZE_SMALL, GxEPD_BLACK);

    } while (display.nextPage());
}

void AppReader::drawReading() {
    // Sem renderizador não há página: acontece se um livro falhar a abrir
    // depois de closeBook() já ter libertado o estado. Cair para a biblioteca
    // é o comportamento visível correcto — desreferenciar era um reset.
    if (!_textRenderer) {
        _state = VIEW_LIBRARY;
        _librarySelectionOnlyRedraw = false;
        drawLibrary();
        return;
    }

    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();
    
    // Check if we need a full refresh
    if (_readingFirstDraw || _pageTurnsSinceRefresh >= _refreshEveryNPages) {
        Serial.println("AppReader: Full Refresh Cycle");
        display.setFullWindow();
        _pageTurnsSinceRefresh = 0;
        _readingFirstDraw = false;
    }
    else { 
        Serial.printf("AppReader: Partial Refresh (%d/%d)\n", _pageTurnsSinceRefresh + 1, _refreshEveryNPages);
        display.setPartialWindow(0, 0, display.width(), display.height()); 
        _pageTurnsSinceRefresh++; 
    }
    
    // Page numbers: use _globalPageNumber which is tracked at runtime
    int currentPageNum = _pageHistory.size();  // For render cache key
    
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        _currentPageRender = _textRenderer->renderRichPageDynamic(display, _currentRichContent,
                                                                _currentPagePointer.nodeIndex,
                                                                _currentPagePointer.charOffset,
                                                                currentPageNum, _globalPageNumber, true);
        _currentPageRenderValid = true;
        // Draw page number directly here for consistent display
        display.setFont(NULL);
        display.setTextColor(GxEPD_BLACK);
        char footerText[40];
        if (_totalPages > 0) {
            snprintf(footerText, sizeof(footerText), "Page %d of %d", _globalPageNumber, _totalPages);
        } else {
            snprintf(footerText, sizeof(footerText), "Page %d", _globalPageNumber);
        }
        int16_t fx1, fy1; uint16_t fw, fh;
        display.getTextBounds(footerText, 0, 0, &fx1, &fy1, &fw, &fh);
        display.setCursor(display.width()/2 - (int)fw/2, display.height() - 15);
        display.print(footerText);
    } while (display.nextPage());
}

void AppReader::update() {
    // Library rendering is static unless input changes selection.

    // Spend a small time-boxed slice on the unified chapter scan (page
    // count + chapter-title index + chapter lengths, see D6), if any of the
    // three still needs it. Runs between draw() calls only (see
    // updateIndexing), so it never overlaps an actual page render.
    if (_indexingActive) updateIndexing();

    // v1.14.0: same budgeted-slice treatment for a pending "go to %" jump.
    // Independent of the scan above — different state, no shared renderer —
    // so both can run in the same session without conflict.
    if (_percentSeekActive) updatePercentSeek();

    // Fora da leitura, aproveitar uma passagem para ler o título de dentro de
    // um EPUB que ainda não o tenha em cache (um por passagem).
    // v1.19.0: no máximo um ZIP aberto por passagem entre título e capa — se
    // já não havia título nenhum por resolver, tenta a capa; caso contrário
    // fica-se pelo título e a capa espera pela próxima passagem. Duplicar o
    // custo de abrir ZIP (e, para a capa, descodificar um JPEG) na mesma
    // passagem arriscava a resposta aos botões que o TODO.txt da v1.17.0 já
    // pede para verificar.
    if (_state == VIEW_LIBRARY && _booksScanned) {
        if (!resolveNextBookTitle()) resolveNextBookCover();
    }

    // Commit a deferred reading position once the page has been still for a
    // while. Page turns only mark it dirty (see saveReadingProgress), so a
    // burst of turns costs one write instead of one per page.
    if (_progressDirty && (millis() - _lastProgressChangeMs) >= PROGRESS_FLUSH_DELAY_MS) {
        flushProgress();
    }
}

void AppReader::applyFontSize(int pt) {
    int normalized = (pt >= 18) ? 18 : (pt >= 12 ? 12 : 9);
    _fontSizePt = normalized;
    if (_textRenderer) _textRenderer->setFontSize(normalized);

    // Re-render the current page from its saved start pointer at the new size.
    // The pointer is a content position (node + char offset), so it's font-size
    // independent; the renderer recomputes where this page ends and the next
    // begins, keeping word-wrap and page breaks consistent.
    _currentPageRenderValid = false;
    _readingFirstDraw = true;     // Full refresh to clear the old layout cleanly
    _pageTurnsSinceRefresh = 0;
    _needsRedraw = true;

    // The total page count is font-size dependent; re-derive it for the new
    // size (a no-op if a cached total already exists at this size). The
    // chapter-title index and lengths startIndexing() also checks are
    // font-independent, so this just confirms their cache hits again.
    startIndexing();
}

void AppReader::applyFontFamily(int family) {
    int normalized =
        (family >= READER_FONT_SANS && family <= READER_FONT_OPEN_SANS) ? family : READER_FONT_SANS;
    _fontFamily = normalized;
    if (_textRenderer) _textRenderer->setFontFamily(normalized);

    // Re-render the current page from its saved start pointer with the new
    // family. Same rationale as applyFontSize: the pointer is a content
    // position, so pagination just recomputes with the new glyph metrics.
    _currentPageRenderValid = false;
    _readingFirstDraw = true;     // Full refresh to clear the old layout cleanly
    _pageTurnsSinceRefresh = 0;
    _needsRedraw = true;

    // Same reasoning as applyFontSize: the total is specific to this family.
    startIndexing();
}

void AppReader::forceRedraw() {
    _librarySelectionOnlyRedraw = false;  // Repaint the whole library view
    _currentPageRenderValid = false;
    _readingFirstDraw = true;             // Repaint the whole reading view
    _needsRedraw = true;
}
