#ifndef TEXT_RENDERER_H
#define TEXT_RENDERER_H

#include <Arduino.h>
#include <vector>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include "DisplayMgr.h"
#include "EpubLoader.h"

struct PagePointer {
    int nodeIndex;
    int charOffset;
};

struct RenderResult {
    int nodesConsumed;
    int charsConsumedInLastNode;
    bool pageFull;
};

struct RenderedLine {
    int x, y, fontSize;
    bool isBold;
    String text;
};

class TextRenderer {
public:
    TextRenderer(int width, int height, int fontSize = 26);
    
    bool loadFont(const uint8_t* data, size_t size);
    void setFontSize(int size) { _fontSize = size; calculateDimensions(); }
    
    void calculateDimensions();

    // New Dynamic Rendering
    RenderResult renderRichPageDynamic(Book32Display& display, const std::vector<ContentNode>& content, 
                                     int startNode, int startOffset, int pageNum, int totalPages, bool draw = true);

    void clearCache() { _lineCache.clear(); }

    // Keep legacy for now to avoid breaking AppReader during transition
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
    int _lineHeight; 
    
    bool _fontLoaded = true; // GFX fonts are always "loaded"
    std::vector<RenderedLine> _lineCache;
    int _cachedPage = -1;
    
    // Fast character width cache
    uint8_t _gfxCharWidths[128];
    const GFXfont* _lastGFXFont = nullptr;

    const GFXfont* getGFXFont(TextStyle style, int& lineHeight);

    std::vector<String> wrapText(const String& text);
    void renderTextNode(Book32Display& display, RichTextNode& node, int& y, int maxY);
    void renderTable(Book32Display& display, Table& table, int& y, int maxY);
};

#endif
