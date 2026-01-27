#ifndef FONT_MGR_H
#define FONT_MGR_H

#include <Arduino.h>
#include <OpenFontRender.h>
#include "DisplayMgr.h"

// Font size presets (in pixels)
#define FONT_SIZE_SMALL     14
#define FONT_SIZE_BODY      18
#define FONT_SIZE_MENU      20
#define FONT_SIZE_SUBTITLE  24
#define FONT_SIZE_TITLE     28
#define FONT_SIZE_HEADER    36

class FontMgr {
public:
    static FontMgr& getInstance();
    
    // Initialize - call after filesystems are mounted
    bool init();
    
    // Check if TTF font is available
    bool hasTTFFont() { return _fontLoaded; }
    
    // Draw text at position (uses TTF if available, falls back to system font)
    void drawText(Book32Display& display, const char* text, int x, int y, int fontSize, uint16_t color = GxEPD_BLACK);
    
    // Draw text centered horizontally
    void drawTextCentered(Book32Display& display, const char* text, int y, int fontSize, uint16_t color = GxEPD_BLACK);
    
    // Draw text right-aligned
    void drawTextRight(Book32Display& display, const char* text, int x, int y, int fontSize, uint16_t color = GxEPD_BLACK);
    
    // Get text width for layout calculations
    int getTextWidth(const char* text, int fontSize);
    
    // Get text height
    int getTextHeight(int fontSize);
    
    // Get the OpenFontRender instance (for advanced usage like in TextRenderer)
    OpenFontRender& getOFR() { return _ofr; }
    
    // Raw font data pointer (for TextRenderer compatibility)
    const uint8_t* getFontData() { return _fontData; }
    size_t getFontDataSize() { return _fontDataSize; }

private:
    FontMgr();
    ~FontMgr();
    
    OpenFontRender _ofr;
    bool _fontLoaded = false;
    uint8_t* _fontData = nullptr;
    size_t _fontDataSize = 0;
    
    bool loadFontFromFS();
};

#endif
