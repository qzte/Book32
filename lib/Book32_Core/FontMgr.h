#ifndef FONT_MGR_H
#define FONT_MGR_H

#include <Arduino.h>
#include "DisplayMgr.h"

// Local FreeSans fonts with Latin-1 Supplement (0x20-0xFF) so Portuguese and
// other Western European text renders correctly across the whole UI. These
// replace the ASCII-only Adafruit <Fonts/FreeSans*pt7b.h> headers.
#include "Fonts/FreeSans.h"

// Font size presets (in pixels) - mapped to GFX fonts
#define FONT_SIZE_SMALL     14   // FreeSans9pt8b
#define FONT_SIZE_BODY      18   // FreeSans12pt8b
#define FONT_SIZE_MENU      20   // FreeSans12pt8b
#define FONT_SIZE_SUBTITLE  24   // FreeSans18pt8b
#define FONT_SIZE_TITLE     28   // FreeSans18pt8b
#define FONT_SIZE_HEADER    36   // FreeSans24pt8b

class FontMgr {
public:
    static FontMgr& getInstance();

    // Initialize
    bool init();

    // Check if font system is ready (always true for GFX fonts)
    bool hasTTFFont() { return true; }

    // Draw text at position using Adafruit GFX fonts
    void drawText(Book32Display& display, const char* text, int x, int y, int fontSize, uint16_t color = GxEPD_BLACK);

    // Draw text centered horizontally
    void drawTextCentered(Book32Display& display, const char* text, int y, int fontSize, uint16_t color = GxEPD_BLACK);

    // Draw text right-aligned
    void drawTextRight(Book32Display& display, const char* text, int x, int y, int fontSize, uint16_t color = GxEPD_BLACK);

    // Get text width for layout calculations
    int getTextWidth(const char* text, int fontSize);

    // Get text height
    int getTextHeight(int fontSize);

    // Get the GFX font for a given size
    const GFXfont* getFont(int fontSize);
    const GFXfont* getFontBold(int fontSize);

    // Convert a UTF-8 string to Latin-1 (ISO-8859-1) bytes for the display
    // layer. Adafruit_GFX::write() consumes one byte per glyph and our fonts
    // cover 0x20-0xFF, so multi-byte UTF-8 sequences must be collapsed first.
    // Codepoints above 0xFF are mapped to ASCII fallbacks where sensible
    // (curly quotes, dashes, ellipsis) or '?' otherwise. NBSP becomes a
    // regular space and soft hyphens are dropped.
    static void utf8ToLatin1(const char* src, char* dst, size_t dstSize);
    static String utf8ToLatin1(const String& src);

private:
    FontMgr();
    ~FontMgr();

    // Character width cache for fast text measurement.
    // 256 entries: covers ASCII + Latin-1 Supplement (must match the glyph
    // range of the fonts, 0x20-0xFF, or width lookups silently break).
    uint8_t _charWidths[256];
    const GFXfont* _lastFont = nullptr;

    void cacheCharWidths(const GFXfont* font);
};

#endif
