#ifndef FONT_MGR_H
#define FONT_MGR_H

#include <Arduino.h>
#include "DisplayMgr.h"

// Include Adafruit GFX FreeSans fonts for consistent styling
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

// Font size presets (in pixels) - mapped to GFX fonts
#define FONT_SIZE_SMALL     14   // FreeSans9pt7b
#define FONT_SIZE_BODY      18   // FreeSans12pt7b
#define FONT_SIZE_MENU      20   // FreeSans12pt7b
#define FONT_SIZE_SUBTITLE  24   // FreeSans18pt7b
#define FONT_SIZE_TITLE     28   // FreeSans18pt7b
#define FONT_SIZE_HEADER    36   // FreeSans24pt7b

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

private:
    FontMgr();
    ~FontMgr();

    // Character width cache for fast text measurement
    uint8_t _charWidths[128];
    const GFXfont* _lastFont = nullptr;

    void cacheCharWidths(const GFXfont* font);
};

#endif
