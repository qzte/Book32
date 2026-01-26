#ifndef TEXT_RENDERER_H
#define TEXT_RENDERER_H

#include <Arduino.h>
#include <vector>
#include "DisplayMgr.h"
#include "EpubLoader.h"  // For ContentNode, RichTextNode, Table, etc.

class TextRenderer {
public:
    TextRenderer(int width, int height, int fontSize = 2);
    
    // Legacy plain text support (backward compatible)
    std::vector<String> paginate(const String& text);
    void renderPage(Book32Display& display, const String& pageText, int pageNum, int totalPages);
    
    // Rich content support
    std::vector<String> paginateRich(std::vector<ContentNode>& content);
    void renderRichPage(Book32Display& display, const String& pageData, int pageNum, int totalPages);

private:
    int _width;
    int _height;
    int _fontSize;
    int _charsPerLine;
    int _linesPerPage;

    // Calculate how many characters fit per line
    void calculateDimensions();

    // Word wrap a line of text
    std::vector<String> wrapText(const String& text);
    
    // Rich content helpers
    int getTextWidth(Book32Display& display, const String& text, int fontSize);
    int getFontHeight(int fontSize);
    void renderTextNode(Book32Display& display, RichTextNode& node, int& y, int maxY);
    void renderTable(Book32Display& display, Table& table, int& y, int maxY);
};

#endif
