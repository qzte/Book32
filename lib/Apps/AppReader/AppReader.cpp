#include "AppReader.h"
#include "DisplayMgr.h"
#include "InputMgr.h"
#include "FontMgr.h"
#include "AppMgr.h"
#include "icon_reader.h"
#include "Book32FS.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <JPEGDEC.h>

static uint8_t* g_coverBitmap = nullptr;
static int g_coverWidth = 0;
static int g_coverHeight = 0;
static int g_targetWidth = 0;
static int g_targetHeight = 0;

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
    _totalBookPages = 0;
    _refreshEveryNPages = 10; // Default to full refresh every 10 pages
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
        }
        file.close();
    }
    // No config file is fine - just use defaults
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

const uint8_t* AppReader::getIconImage() { return icon_reader_80x80; }

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

void AppReader::handleInput(InputAction action) {
    if (action == INPUT_NONE) return;
    if (_state == VIEW_LIBRARY) {
        // Index -1 = "Back to Menu", 0+ = books
        int maxIndex = (int)_books.size() - 1;
        if (action == INPUT_NEXT) {
            _selectedBookIndex++;
            if (_selectedBookIndex > maxIndex) _selectedBookIndex = -1;  // Wrap to Back option
            _needsRedraw = true;
        } else if (action == INPUT_PREV) {
            _selectedBookIndex--;
            if (_selectedBookIndex < -1) _selectedBookIndex = maxIndex;  // Wrap to last book
            _needsRedraw = true;
        } else if (action == INPUT_SELECT) {
            if (_selectedBookIndex == -1) {
                // Back to main menu
                AppMgr::getInstance().switchTo(0);
            } else if (!_books.empty() && _selectedBookIndex >= 0) {
                openBook(_books[_selectedBookIndex].path.c_str());
            }
        }
    } else if (_state == VIEW_READING) {
        if (action == INPUT_NEXT) nextPage();
        else if (action == INPUT_PREV) prevPage();
        else if (action == INPUT_SELECT) { closeBook(); _state = VIEW_LIBRARY; _needsRedraw = true; }
    }
}

void AppReader::calculateTotalPages() {
    _totalBookPages = 0;
    _chapterPageCounts.clear();
    for (int i = 0; i < _epubLoader->getChapterCount(); i++) {
        std::vector<ContentNode> richContent = _epubLoader->getChapterContentRich(i);
        std::vector<String> pages;
        if(richContent.size() > 0) pages = _textRenderer->paginateRich(richContent);
        else pages = _textRenderer->paginate(_epubLoader->getChapterContent(i));
        _chapterPageCounts.push_back(pages.size());
        _totalBookPages += pages.size();
    }
}

int AppReader::getGlobalPageNumber() {
    int page = 0;
    if (_chapterPageCounts.size() > (size_t)_currentChapter) {
        for (int i = 0; i < _currentChapter; i++) page += _chapterPageCounts[i];
    }
    return page + _pageHistory.size() + 1;
}

void AppReader::openBook(const String& path) {
    String fullPath = "/ebooks" + path;
    closeBook();
    _epubLoader = new EpubLoader();
    if (!_epubLoader->open(fullPath.c_str())) { delete _epubLoader; _epubLoader = nullptr; return; }
    _currentBookPath = path;
    if (!_textRenderer) {
        DisplayMgr& dispMgr = DisplayMgr::getInstance();
        Book32Display& display = dispMgr.getDisplay();
        _textRenderer = new TextRenderer(display.width(), display.height(), 18);  // 18px base for reading
    }
    
    // Using Adafruit GFX FreeSans bitmap fonts (same as main menu and all apps)
    Serial.println("TextRenderer: Using Adafruit GFX FreeSans fonts");

    _textRenderer->calculateDimensions();

    // calculateTotalPages(); // DISABLING: This takes forever on large books
    _totalBookPages = 0; // Show simplified pagination for now
    _globalPageNumber = 1; // Start at page 1
    
    loadChapter(0);
    _state = VIEW_READING;
    _needsRedraw = true;
}

void AppReader::closeBook() {
    if (_epubLoader) { _epubLoader->close(); delete _epubLoader; _epubLoader = nullptr; }
    if (_textRenderer) { delete _textRenderer; _textRenderer = nullptr; }
    _pageHistory.clear(); _chapterPageCounts.clear(); _totalBookPages = 0;
}

void AppReader::loadChapter(int chapterIndex) {
    if (!_epubLoader) return;
    if (chapterIndex < 0 || chapterIndex >= _epubLoader->getChapterCount()) return;
    
    int originalIndex = chapterIndex;
    while (chapterIndex < _epubLoader->getChapterCount()) {
        _currentChapter = chapterIndex;
        _pageHistory.clear();
        _currentPagePointer = {0, 0};
        
        _currentRichContent = _epubLoader->getChapterContentRich(chapterIndex);
        if (_currentRichContent.size() > 0) {
            if (_textRenderer) _textRenderer->clearCache();
            _needsRedraw = true;
            return;
        }
        chapterIndex++;
    }
    _currentChapter = originalIndex;
    if (_textRenderer) _textRenderer->clearCache();
    _needsRedraw = true;
}

void AppReader::nextPage() {
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();
    
    // Calculate what the CURRENT page contains (this fills the cache for drawing)
    int currentPageNum = _pageHistory.size(); // 0-indexed current page within chapter
    RenderResult result = _textRenderer->renderRichPageDynamic(display, _currentRichContent, 
                                                            _currentPagePointer.nodeIndex, 
                                                            _currentPagePointer.charOffset, 
                                                            currentPageNum, 0, false); // 0 for pageNumForDisplay - not used
    
    if (result.pageFull) {
        // Save current position to history before advancing
        _pageHistory.push_back(_currentPagePointer);
        
        // Calculate next page start position
        _currentPagePointer.nodeIndex += result.nodesConsumed;
        _currentPagePointer.charOffset = result.charsConsumedInLastNode;
        
        // Increment global page counter
        _globalPageNumber++;
        
        // Clear cache since we're moving to a new page
        _textRenderer->clearCache();
        _needsRedraw = true;
    } else {
        // End of chapter - advance to next
        if (_currentChapter < _epubLoader->getChapterCount() - 1) {
            // Save current chapter state to history
            _pageHistory.push_back(_currentPagePointer);
            _globalPageNumber++; // Next page in next chapter
            loadChapter(_currentChapter + 1);
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
        _needsRedraw = true;
    } else {
        // Go to previous chapter
        if (_currentChapter > 0) {
            // NOTE: Going to the "last page" of the previous chapter is tricky
            // because we don't know where it starts without rendering it.
            // For now, we go to the start of the previous chapter.
            if (_globalPageNumber > 1) _globalPageNumber--; // Decrement for prev chapter
            prevChapter();
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
    FontMgr& fontMgr = FontMgr::getInstance();

    const int BACK_ITEM_HEIGHT = 50;
    const int COVER_WIDTH = 60;
    const int COVER_HEIGHT = 80;
    const int ITEM_HEIGHT = 100;
    const int ITEM_PADDING = 20;
    const int TEXT_X = COVER_WIDTH + 40;

    // Use Partial Refresh for Library interactions
    display.setPartialWindow(0, 0, display.width(), display.height());

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // Header "Library" (24px)
        fontMgr.drawText(display, "Library", 15, 35, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        display.drawLine(0, 50, display.width(), 50, GxEPD_BLACK);

        int y = 55;

        // === "Back to Menu" option (index -1) ===
        bool backSelected = (_selectedBookIndex == -1);
        if (backSelected) {
            display.fillRect(0, y, display.width(), BACK_ITEM_HEIGHT, GxEPD_BLACK);
        }
        // Draw back arrow and text
        uint16_t backColor = backSelected ? GxEPD_WHITE : GxEPD_BLACK;
        fontMgr.drawText(display, "<  Back to Menu", ITEM_PADDING, y + 32, FONT_SIZE_MENU, backColor);
        if (!backSelected) {
            display.drawLine(ITEM_PADDING, y + BACK_ITEM_HEIGHT - 2, display.width() - ITEM_PADDING, y + BACK_ITEM_HEIGHT - 2, GxEPD_BLACK);
        }
        y += BACK_ITEM_HEIGHT;

        // === Book list ===
        if (_books.empty()) {
            fontMgr.drawText(display, "No books found.", 20, y + 30, FONT_SIZE_BODY, GxEPD_BLACK);
            fontMgr.drawText(display, "Upload EPUBs via web.", 20, y + 60, FONT_SIZE_BODY, GxEPD_BLACK);
        } else {
            int idx = 0;
            for (const auto& book : _books) {
                if (y > display.height() - 60) break;

                bool isSelected = (idx == _selectedBookIndex);
                if (isSelected) display.fillRect(0, y, display.width(), ITEM_HEIGHT, GxEPD_BLACK);

                int coverX = ITEM_PADDING;
                int coverY = y + 10;
                BookEntry& bookRef = _books[idx];
                drawCover(display, bookRef, coverX, coverY, COVER_WIDTH, COVER_HEIGHT, isSelected);

                uint16_t textColor = isSelected ? GxEPD_WHITE : GxEPD_BLACK;

                // Draw book title with word wrapping
                String title = book.title;
                int textY = y + 28;
                int lineCount = 0;
                const int MAX_LINES = 3;
                const int MAX_WIDTH = display.width() - TEXT_X - 20;

                int pos = 0;
                while (pos < (int)title.length() && lineCount < MAX_LINES) {
                    String line = "";
                    while (pos < (int)title.length()) {
                        int nextSpace = title.indexOf(' ', pos);
                        if (nextSpace == -1) nextSpace = title.length();
                        String word = title.substring(pos, nextSpace);
                        String testLine = line.length() > 0 ? line + " " + word : word;
                        if (fontMgr.getTextWidth(testLine.c_str(), FONT_SIZE_MENU) > MAX_WIDTH && line.length() > 0) break;
                        line = testLine;
                        pos = nextSpace + 1;
                    }
                    if (lineCount == MAX_LINES - 1 && pos < (int)title.length()) {
                        line = line.substring(0, line.length() - 3) + "...";
                    }
                    fontMgr.drawText(display, line.c_str(), TEXT_X, textY, FONT_SIZE_MENU, textColor);
                    textY += 24;
                    lineCount++;
                }

                if (!isSelected) display.drawLine(ITEM_PADDING, y + ITEM_HEIGHT - 2, display.width() - ITEM_PADDING, y + ITEM_HEIGHT - 2, GxEPD_BLACK);
                y += ITEM_HEIGHT;
                idx++;
            }
        }

        // Page indicator (14px) - show current selection
        char pageStr[24];
        if (_selectedBookIndex == -1) {
            snprintf(pageStr, sizeof(pageStr), "Menu");
        } else {
            snprintf(pageStr, sizeof(pageStr), "%d/%d", _selectedBookIndex + 1, (int)_books.size());
        }
        fontMgr.drawTextRight(display, pageStr, display.width() - 10, display.height() - 20, FONT_SIZE_SMALL, GxEPD_BLACK);

    } while (display.nextPage());
}

void AppReader::drawReading() {
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();
    
    // Check if we need a full refresh
    static bool firstDraw = true;
    if (firstDraw || _pageTurnsSinceRefresh >= _refreshEveryNPages) { 
        Serial.println("AppReader: Full Refresh Cycle");
        display.setFullWindow(); 
        _pageTurnsSinceRefresh = 0; 
        firstDraw = false;
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
        _textRenderer->renderRichPageDynamic(display, _currentRichContent, 
                                           _currentPagePointer.nodeIndex, 
                                           _currentPagePointer.charOffset, 
                                           currentPageNum, _globalPageNumber, true);
    } while (display.nextPage());
}

void AppReader::update() {}
