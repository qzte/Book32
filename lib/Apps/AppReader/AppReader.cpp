#include "AppReader.h"
#include "DisplayMgr.h"
#include "InputMgr.h"
#include "icon_reader.h"
#include "Book32FS.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <JPEGDEC.h>

// Global for JPEG decoder callback
static uint8_t* g_coverBitmap = nullptr;
static int g_coverWidth = 0;
static int g_coverHeight = 0;
static int g_targetWidth = 0;
static int g_targetHeight = 0;

// JPEG decoder callback - draws to bitmap buffer with Atkinson dithering
int jpegDrawCallback(JPEGDRAW *pDraw) {
    if (!g_coverBitmap) return 0;
    static int16_t* errorBuffer = nullptr;
    if (pDraw->y == 0 && pDraw->x == 0) {
        if (errorBuffer) free(errorBuffer);
        errorBuffer = (int16_t*)calloc(g_targetWidth * g_targetHeight, sizeof(int16_t));
    }
    float scaleX = (float)g_targetWidth / g_coverWidth;
    float scaleY = (float)g_targetHeight / g_coverHeight;
    for (int y = 0; y < pDraw->iHeight; y++) {
        int srcY = pDraw->y + y;
        int dstY = (int)(srcY * scaleY);
        if (dstY >= g_targetHeight) continue;
        for (int x = 0; x < pDraw->iWidth; x++) {
            int srcX = pDraw->x + x;
            int dstX = (int)(srcX * scaleX);
            if (dstX >= g_targetWidth) continue;
            uint16_t pixel = pDraw->pPixels[y * pDraw->iWidth + x];
            int r = ((pixel >> 11) & 0x1F) << 3;
            int g = ((pixel >> 5) & 0x3F) << 2;
            int b = (pixel & 0x1F) << 3;
            int gray = (r * 30 + g * 59 + b * 11) / 100;
            int currentError = errorBuffer ? errorBuffer[dstY * g_targetWidth + dstX] : 0;
            int correctedGray = gray + currentError;
            int finalPixel = (correctedGray < 128) ? 0 : 255;
            int error = correctedGray - finalPixel;
            int byteIndex = (dstY * g_targetWidth + dstX) / 8;
            int bitIndex = 7 - ((dstY * g_targetWidth + dstX) % 8);
            if (finalPixel == 0) g_coverBitmap[byteIndex] |= (1 << bitIndex);
            auto addError = [&](int dx, int dy, int weight) {
                int tx = dstX + dx;
                int ty = dstY + dy;
                if (tx >= 0 && tx < g_targetWidth && ty >= 0 && ty < g_targetHeight && errorBuffer) errorBuffer[ty * g_targetWidth + tx] += (error * weight) >> 3;
            };
            addError(1, 0, 1); addError(2, 0, 1); addError(-1, 1, 1); addError(0, 1, 1); addError(1, 1, 1); addError(0, 2, 1);
        }
    }
    if (pDraw->y + pDraw->iHeight >= g_coverHeight && errorBuffer) { free(errorBuffer); errorBuffer = nullptr; }
    return 1;
}

AppReader::AppReader() {
    _state = VIEW_LIBRARY;
    _selectedBookIndex = 0;
    _scrollOffset = 0;
    _booksScanned = false;
    _epubLoader = nullptr;
    _textRenderer = nullptr;
    _currentChapter = 0;
    _currentPage = 0;
    _needsRedraw = true;
    _pageTurnsSinceRefresh = 0;
    loadSettings();
}

void AppReader::loadSettings() {
    File file = SystemFS.open("/reader_config.json", "r");
    if (file) {
        DynamicJsonDocument doc(512);
        if (!deserializeJson(doc, file)) {
            if (doc.containsKey("refreshFrequency")) _refreshEveryNPages = doc["refreshFrequency"];
        }
        file.close();
    }
}

AppReader::~AppReader() {
    closeBook();
    if (_epubLoader) delete _epubLoader;
    if (_textRenderer) delete _textRenderer;
}

void AppReader::start() {
    _state = VIEW_LIBRARY;
    _needsRedraw = true;
    InputMgr::getInstance().setCallback(std::bind(&AppReader::handleInput, this, std::placeholders::_1));
}

void AppReader::stop() {
    closeBook();
    InputMgr::getInstance().clearCallback();
}

const uint8_t* AppReader::getIconImage() { return icon_reader_48x48; }

void AppReader::scanBooks() {
    _books.clear();
    File root = EbookFS.open("/");
    if(!root || !root.isDirectory()) return;
    File file = root.openNextFile();
    while(file){
        String fileName = file.name();
        if(fileName.endsWith(".epub")) {
            BookEntry entry;
            entry.path = "/" + fileName;
            EpubLoader tempLoader;
            if(tempLoader.open(("/ebooks" + entry.path).c_str())) {
                entry.title = tempLoader.getTitle();
                tempLoader.close();
            } else {
                String title = fileName;
                if(title.startsWith("/")) title = title.substring(1);
                if(title.endsWith(".epub")) title = title.substring(0, title.length()-5);
                entry.title = title;
            }
            entry.hasCover = false;
            entry.coverBitmap = nullptr;
            entry.coverWidth = 60;
            entry.coverHeight = 80;
            String thumbPath = "/covers/" + fileName;
            thumbPath.replace(".epub", ".thumb");
            if(EbookFS.exists(thumbPath)) {
                File thumbFile = EbookFS.open(thumbPath, "r");
                if(thumbFile) {
                    size_t thumbSize = thumbFile.size();
                    if(thumbSize == 600) {
                        entry.coverBitmap = (uint8_t*)ps_malloc(thumbSize);
                        if(!entry.coverBitmap) entry.coverBitmap = (uint8_t*)malloc(thumbSize);
                        if(entry.coverBitmap) { thumbFile.read(entry.coverBitmap, thumbSize); entry.hasCover = true; }
                    }
                    thumbFile.close();
                }
            }
            _books.push_back(entry);
        }
        file = root.openNextFile();
    }
}

bool AppReader::loadBookCover(BookEntry& book) {
    if (book.coverBitmap) return true;
    EpubLoader tempLoader;
    String fullPath = "/ebooks" + book.path;
    if (!tempLoader.open(fullPath.c_str())) return false;
    size_t coverSize = 0;
    uint8_t* coverData = tempLoader.getCoverData(&coverSize);
    tempLoader.close();
    if (!coverData || coverSize == 0) return false;
    JPEGDEC jpeg;
    if (!jpeg.openRAM(coverData, coverSize, jpegDrawCallback)) { free(coverData); return false; }
    g_coverWidth = jpeg.getWidth();
    g_coverHeight = jpeg.getHeight();
    g_targetWidth = 60;
    g_targetHeight = 80;
    int bitmapSize = (g_targetWidth * g_targetHeight + 7) / 8;
    g_coverBitmap = (uint8_t*)ps_malloc(bitmapSize);
    if (!g_coverBitmap) g_coverBitmap = (uint8_t*)malloc(bitmapSize);
    if (!g_coverBitmap) { jpeg.close(); free(coverData); return false; }
    memset(g_coverBitmap, 0, bitmapSize);
    jpeg.setPixelType(RGB565_BIG_ENDIAN);
    jpeg.decode(0, 0, 0);
    jpeg.close();
    free(coverData);
    book.coverBitmap = g_coverBitmap;
    book.coverWidth = g_targetWidth;
    book.coverHeight = g_targetHeight;
    book.hasCover = true;
    g_coverBitmap = nullptr;
    return true;
}

void AppReader::drawCover(Book32Display& display, BookEntry& book, int x, int y, int w, int h, bool inverted) {
    if (!book.hasCover || !book.coverBitmap) {
        if (inverted) display.fillRect(x, y, w, h, GxEPD_WHITE);
        else display.drawRect(x, y, w, h, GxEPD_BLACK);
        display.setTextColor(inverted ? GxEPD_BLACK : GxEPD_BLACK);
        display.setTextSize(1);
        display.setCursor(x + w/2 - 12, y + h/2 - 4);
        display.print("EPUB");
        return;
    }
    for (int py = 0; py < book.coverHeight && py < h; py++) {
        for (int px = 0; px < book.coverWidth && px < w; px++) {
            int byteIndex = (py * book.coverWidth + px) / 8;
            int bitIndex = 7 - ((py * book.coverWidth + px) % 8);
            bool isBlack = (book.coverBitmap[byteIndex] >> bitIndex) & 1;
            if (inverted) isBlack = !isBlack;
            if (isBlack) display.drawPixel(x + px, y + py, GxEPD_BLACK);
            else display.drawPixel(x + px, y + py, GxEPD_WHITE);
        }
    }
}

void AppReader::update() {}

void AppReader::handleInput(InputAction action) {
    if (action == INPUT_NONE) return;
    if (_state == VIEW_LIBRARY) {
        if (action == INPUT_NEXT) {
            _selectedBookIndex++;
            if (_selectedBookIndex >= (int)_books.size()) _selectedBookIndex = 0;
            _needsRedraw = true;
        } else if (action == INPUT_PREV) {
            _selectedBookIndex--;
            if (_selectedBookIndex < 0) _selectedBookIndex = _books.size() - 1;
            _needsRedraw = true;
        } else if (action == INPUT_SELECT) {
             if(!_books.empty()) openBook(_books[_selectedBookIndex].path.c_str());
        }
    } else if (_state == VIEW_READING) {
        if (action == INPUT_NEXT) nextPage();
        else if (action == INPUT_PREV) prevPage();
        else if (action == INPUT_SELECT) { closeBook(); _state = VIEW_LIBRARY; _needsRedraw = true; }
    }
}

void AppReader::openBook(const String& path) {
    String fullPath = "/ebooks" + path;
    Serial.printf("Opening book: %s\n", fullPath.c_str());
    closeBook();
    _epubLoader = new EpubLoader();
    if (!_epubLoader->open(fullPath.c_str())) { delete _epubLoader; _epubLoader = nullptr; return; }
    _currentBookPath = path;
    if (!_textRenderer) {
        DisplayMgr& dispMgr = DisplayMgr::getInstance();
        Book32Display& display = dispMgr.getDisplay();
        _textRenderer = new TextRenderer(display.width(), display.height(), FONT_SIZE_DEFAULT);
    }

    // FONT LOADING LOGIC
    bool fontLoaded = false;
    
    // 1. Try to load from the EPUB itself
    std::vector<FontInfo> fonts = _epubLoader->getFonts();
    if (!fonts.empty()) {
        int fontIdx = 0;
        for (size_t i = 0; i < fonts.size(); i++) { if (fonts[i].style == "normal") { fontIdx = i; break; } }
        size_t fontSize = 0;
        uint8_t* fontData = _epubLoader->getFontData(fonts[fontIdx].path, &fontSize);
        if (fontData && fontSize > 0) {
            if (_textRenderer->loadFont(fontData, fontSize)) {
                Serial.printf("Loaded EPUB font: %s\n", fonts[fontIdx].family.c_str());
                fontLoaded = true;
            } else { free(fontData); }
        }
    }

    // 2. If no font in EPUB, try to load global fallback font from EbookFS
    if (!fontLoaded && EbookFS.exists("/font.ttf")) {
        File f = EbookFS.open("/font.ttf", "r");
        if (f) {
            size_t s = f.size();
            uint8_t* d = (uint8_t*)ps_malloc(s);
            if (d) {
                f.read(d, s);
                if (_textRenderer->loadFont(d, s)) {
                    Serial.println("Loaded global fallback font from /ebooks/font.ttf");
                    fontLoaded = true;
                } else { free(d); }
            }
            f.close();
        }
    }
    
    loadChapter(0);
    _state = VIEW_READING;
    _needsRedraw = true;
}

void AppReader::closeBook() {
    if (_epubLoader) { _epubLoader->close(); delete _epubLoader; _epubLoader = nullptr; }
    if (_textRenderer) { delete _textRenderer; _textRenderer = nullptr; }
    _pages.clear();
}

void AppReader::loadChapter(int chapterIndex) {
    if (!_epubLoader) return;
    if (chapterIndex < 0 || chapterIndex >= _epubLoader->getChapterCount()) return;
    if (!_textRenderer) {
        DisplayMgr& dispMgr = DisplayMgr::getInstance();
        Book32Display& display = dispMgr.getDisplay();
        _textRenderer = new TextRenderer(display.width(), display.height(), FONT_SIZE_DEFAULT);
    }
    int originalIndex = chapterIndex;
    while (chapterIndex < _epubLoader->getChapterCount()) {
        _currentChapter = chapterIndex;
        _currentPage = 0;
        std::vector<ContentNode> richContent = _epubLoader->getChapterContentRich(chapterIndex);
        if(richContent.size() > 0) _pages = _textRenderer->paginateRich(richContent);
        else _pages = _textRenderer->paginate(_epubLoader->getChapterContent(chapterIndex));
        if (_pages.size() > 0) { _needsRedraw = true; return; }
        chapterIndex++;
    }
    _currentChapter = originalIndex;
    _needsRedraw = true;
}

void AppReader::nextPage() {
    if (_currentPage < (int)_pages.size() - 1) { _currentPage++; _needsRedraw = true; }
    else nextChapter();
}

void AppReader::prevPage() {
    if (_currentPage > 0) { _currentPage--; _needsRedraw = true; }
    else prevChapter();
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
            String chapterText = _epubLoader->getChapterContent(tryChapter);
            if (chapterText.length() > 0) { loadChapter(tryChapter); return; }
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

void AppReader::drawLibrary() {
    if (!_booksScanned) { scanBooks(); _booksScanned = true; }
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();
    const int COVER_WIDTH = 60;
    const int COVER_HEIGHT = 80;
    const int ITEM_HEIGHT = 100;
    const int ITEM_PADDING = 20;
    const int TEXT_X = COVER_WIDTH + 40;
    const int CHARS_PER_LINE = 30;
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.setTextSize(3);
        display.setCursor(10, 10);
        display.print("Library");
        display.drawLine(0, 45, display.width(), 45, GxEPD_BLACK);
        if (_books.empty()) {
            display.setCursor(20, 60);
            display.setTextSize(2);
            display.println("No books found.");
            display.println("Upload EPUBs via web.");
            continue;
        }
        int y = 50;
        int idx = 0;
        for (const auto& book : _books) {
            bool isSelected = (idx == _selectedBookIndex);
            if (isSelected) display.fillRect(0, y, display.width(), ITEM_HEIGHT, GxEPD_BLACK);
            int coverX = ITEM_PADDING;
            int coverY = y + 10;
            BookEntry& bookRef = _books[idx];
            drawCover(display, bookRef, coverX, coverY, COVER_WIDTH, COVER_HEIGHT, isSelected);
            display.setTextColor(isSelected ? GxEPD_WHITE : GxEPD_BLACK);
            display.setTextSize(2);
            String title = book.title;
            int textY = y + 20;
            int lineCount = 0;
            const int MAX_LINES = 3;
            int pos = 0;
            int titleLen = title.length();
            while (pos < titleLen && lineCount < MAX_LINES) {
                int remaining = titleLen - pos;
                int lineLen = (remaining < CHARS_PER_LINE) ? remaining : CHARS_PER_LINE;
                if (pos + lineLen < titleLen) {
                    int breakPos = -1;
                    for (int i = pos + lineLen - 1; i > pos; i--) { if (title.charAt(i) == ' ') { breakPos = i; break; } }
                    if (breakPos > pos) lineLen = breakPos - pos;
                }
                String line = title.substring(pos, pos + lineLen);
                line.trim();
                if (lineCount == MAX_LINES - 1 && pos + lineLen < titleLen) { if (line.length() > 3) line = line.substring(0, line.length() - 3) + "..."; }
                if (line.length() > 0) { display.setCursor(TEXT_X, textY); display.print(line); textY += 12; lineCount++; }
                pos += lineLen;
                while (pos < titleLen && title.charAt(pos) == ' ') pos++;
            }
            if (!isSelected) display.drawLine(ITEM_PADDING, y + ITEM_HEIGHT - 2, display.width() - ITEM_PADDING, y + ITEM_HEIGHT - 2, GxEPD_BLACK);
            y += ITEM_HEIGHT;
            idx++;
            if (y > display.height() - 50) break;
        }
        if (_books.size() > 1) {
            display.setTextSize(2);
            display.setTextColor(GxEPD_BLACK);
            display.setCursor(display.width() - 80, display.height() - 30);
            display.printf("%d/%d", _selectedBookIndex + 1, _books.size());
        }
    } while (display.nextPage());
}

void AppReader::drawReading() {
    if (_pages.empty()) return;
    if (_currentPage >= (int)_pages.size()) return;
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();
    if (_pageTurnsSinceRefresh >= _refreshEveryNPages) { display.setFullWindow(); _pageTurnsSinceRefresh = 0; }
    else { display.setPartialWindow(0, 0, display.width(), display.height()); _pageTurnsSinceRefresh++; }
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.setTextSize(FONT_SIZE_DEFAULT);
        String pageText = _pages[_currentPage];
        int totalPages = _pages.size();
        if(pageText.startsWith("T:") || pageText.startsWith("TABLE:")) _textRenderer->renderRichPage(display, pageText, _currentPage, totalPages);
        else _textRenderer->renderPage(display, pageText, _currentPage, totalPages);
    } while (display.nextPage());
}
