#ifndef TEXT_RENDERER_H
#define TEXT_RENDERER_H

#include <Arduino.h>
#include <vector>
#include <Adafruit_GFX.h>
// Local FreeSans with Latin-1 Supplement (0x20-0xFF); replaces the ASCII-only
// Adafruit <Fonts/FreeSans*pt7b.h> headers so Portuguese text renders in the
// reader when the Sans family is selected.
#include "Fonts/FreeSans.h"
#include "Fonts/Merriweather.h"
#include "Fonts/Literata.h"
#include "Fonts/SourceSerif4.h"
#include "Fonts/Gelasio.h"
#include "DisplayMgr.h"
#include "EpubLoader.h"

// Reading font family, selectable in the web UI's Reader Options.
enum ReaderFontFamily {
    READER_FONT_SANS         = 0, // FreeSans (system default, no serif)
    READER_FONT_MERRIWEATHER = 1, // Bookerly-style serif (SIL OFL substitute)
    READER_FONT_LITERATA     = 2, // Literata (SIL OFL)
    READER_FONT_SOURCE_SERIF = 3, // Source Serif 4 / "Source Serif Pro" (SIL OFL)
    READER_FONT_GELASIO      = 4  // Georgia-style serif (SIL OFL substitute)
};

struct PagePointer {
    int nodeIndex;
    int charOffset;
};

struct RenderResult {
    int nodesConsumed;
    int charsConsumedInLastNode;
    bool pageFull;
    int nextNodeIndex;
    int nextCharOffset;
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
    // Body text size in points. Supported: 9 (small), 12 (medium), 18 (large).
    // Invalidates caches so word-wrap and pagination recompute at the new size.
    void setFontSize(int size);
    int getFontSize() const { return _fontSize; }

    // Reading font family (see ReaderFontFamily). Invalidates caches so
    // word-wrap and pagination recompute with the new glyph metrics.
    void setFontFamily(int family);
    int getFontFamily() const { return _fontFamily; }

    void calculateDimensions();

    // New Dynamic Rendering
    RenderResult renderRichPageDynamic(Book32Display& display, const std::vector<ContentNode>& content, 
                                     int startNode, int startOffset, int pageNum, int pageNumForDisplay, bool draw = true);

    void clearCache();

    // Keep legacy for now to avoid breaking AppReader during transition
    std::vector<String> paginate(const String& text);
    void renderPage(Book32Display& display, const String& pageText, int pageNum, int totalPages);
    
    std::vector<String> paginateRich(std::vector<ContentNode>& content);
    void renderRichPage(Book32Display& display, const String& pageData, int pageNum, int totalPages);

private:
    int _width;
    int _height;
    int _fontSize;
    int _fontFamily = READER_FONT_SANS;
    int _charsPerLine;
    int _linesPerPage;
    int _lineHeight; 
    
    bool _fontLoaded = true; // GFX fonts are always "loaded"
    std::vector<RenderedLine> _lineCache;
    int _cachedPage = -1;
    RenderResult _cachedResult = {0, 0, false, 0, 0};
    bool _hasCachedResult = false;
    
    // Fast character width cache.
    // 256 entries: covers ASCII + Latin-1 Supplement (must match the glyph
    // range of the fonts, 0x20-0xFF, or word-wrap widths silently break).
    uint8_t _gfxCharWidths[256];
    const GFXfont* _lastGFXFont = nullptr;

    const GFXfont* getGFXFont(TextStyle style, int& lineHeight);

    std::vector<String> wrapText(const String& text);
    void renderTextNode(Book32Display& display, RichTextNode& node, int& y, int maxY);
    void renderTable(Book32Display& display, Table& table, int& y, int maxY);
};

#endif
