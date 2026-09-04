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
#include "Fonts/OpenSans.h"
#include "DisplayMgr.h"
#include "EpubLoader.h"

// Reading font family, selectable in the web UI's Reader Options.
enum ReaderFontFamily {
    READER_FONT_SANS = 0,         // FreeSans (system default, no serif)
    READER_FONT_MERRIWEATHER = 1, // Bookerly-style serif (SIL OFL substitute)
    READER_FONT_LITERATA = 2,     // Literata (SIL OFL)
    READER_FONT_SOURCE_SERIF = 3, // Source Serif 4 / "Source Serif Pro" (SIL OFL)
    READER_FONT_GELASIO = 4,      // Georgia-style serif (SIL OFL substitute)
    READER_FONT_OPEN_SANS = 5     // Open Sans, humanist sans-serif (SIL OFL)
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
    
    // Body text size in points. Supported: 9 (small), 12 (medium), 18 (large).
    // Invalidates caches so word-wrap and pagination recompute at the new size.
    void setFontSize(int size);
    int getFontSize() const { return _fontSize; }

    // Reading font family (see ReaderFontFamily). Invalidates caches so
    // word-wrap and pagination recompute with the new glyph metrics.
    void setFontFamily(int family);
    int getFontFamily() const { return _fontFamily; }

    void calculateDimensions();

    // New Dynamic Rendering. `loader`, when not null, is the EpubLoader the
    // content came from — needed to read an in-chapter image's bytes out of
    // the EPUB zip (see renderImageNode() in TextRenderer.cpp). A caller that
    // never passes CONTENT_IMAGE nodes (or doesn't have the loader handy) can
    // leave it null: image nodes then just measure as zero-height and are
    // skipped, same as an unrecognized node type always has been.
    RenderResult renderRichPageDynamic(Book32Display& display, const std::vector<ContentNode>& content,
                                       int startNode, int startOffset, int pageNum, int pageNumForDisplay,
                                       bool draw = true, EpubLoader* loader = nullptr);

    void clearCache();

private:
    int _width;
    int _height;
    int _fontSize;
    int _fontFamily = READER_FONT_SANS;
    int _lineHeight;
    
    std::vector<RenderedLine> _lineCache;
    int _cachedPage = -1;
    RenderResult _cachedResult = {0, 0, false, 0, 0};
    bool _hasCachedResult = false;
    // true when the cached page includes a drawn image: _lineCache only ever
    // records text runs (see renderTextNode's _lineCache.push_back sites), so
    // the draw-from-cache fast path in renderRichPageDynamic would silently
    // drop the image on a same-page redraw. Bypasses the fast path for that
    // page instead — a full recompute is the only way to redraw an image.
    bool _cachedHasImage = false;

    // Fast character width cache.
    // 256 entries: covers ASCII + Latin-1 Supplement (must match the glyph
    // range of the fonts, 0x20-0xFF, or word-wrap widths silently break).
    uint8_t _gfxCharWidths[256];
    const GFXfont* _lastGFXFont = nullptr;

    const GFXfont* getGFXFont(TextStyle style, int& lineHeight);

    // Word-wraps (and, when draw, draws) one CONTENT_TEXT node; see the
    // definition in TextRenderer.cpp for the full contract. Extracted out
    // of renderRichPageDynamic() so the page-level node loop and the
    // line/word-wrap loop aren't nested inside each other (M2).
    bool renderTextNode(Book32Display& display, const std::vector<ContentNode>& content, int currentNode,
                        int currentOffset, int& y, int maxY, int& currentX, int& line_width,
                        bool& justHyphenated, bool draw, RenderResult& full);

    // Lays out (and, when draw, draws) one CONTENT_IMAGE node as a block that
    // owns its own line, sized to fit the text column without changing the
    // book's aspect ratio. Returns true when the image doesn't fit in what's
    // left of the current page and the whole node must move to the next page
    // (mirrors renderTextNode's "page full" contract, but never partially
    // consumes an image — it's atomic).
    bool renderImageNode(Book32Display& display, const ContentNode& node, int& y, int maxY, int& currentX,
                         int& line_width, bool draw, EpubLoader* loader);
};

#endif
