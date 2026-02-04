#ifndef APP_READER_H
#define APP_READER_H

#include "BaseApp.h"
#include "EpubLoader.h"
#include "TextRenderer.h"
#include "../../Book32_Core/InputMgr.h"
#include <vector>
#include <map>

enum ReaderState {
    VIEW_LIBRARY,
    VIEW_READING
};

struct BookEntry {
    String path;  // Full path to file
    String title; // Display title
    bool hasCover;  // Whether cover was found
    uint8_t* coverBitmap;  // Dithered 1-bit bitmap for e-ink (60x80 pixels = 600 bytes)
    int coverWidth;
    int coverHeight;
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

    void handleInput(InputAction action);

private:
    ReaderState _state;
    
    // Library
    std::vector<BookEntry> _books;
    int _selectedBookIndex;
    int _scrollOffset; // For list scrolling
    bool _booksScanned;
    void scanBooks();
    void drawLibrary();
    bool loadBookCover(BookEntry& book);  // Load and decode cover for a book
    void drawCover(Book32Display& display, BookEntry& book, int x, int y, int w, int h, bool inverted);
    
    // Global Pagination
    int _totalBookPages;
    std::vector<int> _chapterPageCounts;
    void calculateTotalPages();
    int getGlobalPageNumber();
    
    // Settings
    int _refreshEveryNPages;
    int _pageTurnsSinceRefresh;
    void loadSettings();
    
    // Reading
    EpubLoader* _epubLoader;
    TextRenderer* _textRenderer;
    String _currentBookPath;
    int _currentChapter;
    int _currentPage; // Current page number within the whole book
    int _globalPageNumber; // Runtime tracking of global page (1-indexed)
    bool _needsRedraw;
    
    // Dynamic Pagination
    std::vector<ContentNode> _currentRichContent;
    PagePointer _currentPagePointer;
    std::vector<PagePointer> _pageHistory; // Stores start of each page for current chapter
    
    void openBook(const String& path);
    void closeBook();
    void loadChapter(int chapterIndex);
    void nextPage();
    void prevPage();
    void nextChapter();
    void prevChapter();
    void drawReading();
};

#endif
