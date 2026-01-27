#ifndef TEXT_RENDERER_H
#define TEXT_RENDERER_H

#include <Arduino.h>
#include <vector>
#include <OpenFontRender.h>
#include "DisplayMgr.h"
#include "EpubLoader.h"

class TextRenderer {
public:
    TextRenderer(int width, int height, int fontSize = 26);
    
    bool loadFont(const uint8_t* data, size_t size);
    void setFontSize(int size) { _fontSize = size; calculateDimensions(); }
    
    // Make public so AppReader can trigger recalculation after font load
    void calculateDimensions();

    std::vector<String> paginate(const String& text);
    void renderPage(Book32Display& display, const String& pageText, int pageNum, int totalPages);
    
    std::vector<String> paginateRich(std::vector<ContentNode>& content);
    void renderRichPage(Book32Display& display, const String& pageData, int pageNum, int totalPages);

private:
    int _width;
    int _height;
    int _fontSize;
    int _charsPerLine;
    int _linesPerPage;
    int _lineHeight; // Added this
    
    OpenFontRender _ofr;
    bool _fontLoaded = false;

    std::vector<String> wrapText(const String& text);
    void renderTextNode(Book32Display& display, RichTextNode& node, int& y, int maxY);
    void renderTable(Book32Display& display, Table& table, int& y, int maxY);
};

#endif
