#include "FontMgr.h"
#include "Book32FS.h"

FontMgr::FontMgr() {}

FontMgr::~FontMgr() {
    if (_fontData) {
        free(_fontData);
        _fontData = nullptr;
    }
}

FontMgr& FontMgr::getInstance() {
    static FontMgr instance;
    return instance;
}

bool FontMgr::loadFontFromFS() {
    // Try EbookFS first (where uploadfs puts files)
    const char* fontPaths[] = {
        "/DejaVuSerif.ttf",
        "/font.ttf"
    };
    
    for (const char* fontPath : fontPaths) {
        // Try EbookFS
        if (EbookFS.exists(fontPath)) {
            Serial.printf("FontMgr: Found %s in EbookFS\n", fontPath);
            File f = EbookFS.open(fontPath, "r");
            if (f) {
                _fontDataSize = f.size();
                _fontData = (uint8_t*)ps_malloc(_fontDataSize);
                if (!_fontData) _fontData = (uint8_t*)malloc(_fontDataSize);
                
                if (_fontData) {
                    f.read(_fontData, _fontDataSize);
                    f.close();
                    
                    FT_Error err = _ofr.loadFont(_fontData, _fontDataSize);
                    if (err == 0) {
                        Serial.printf("FontMgr: Loaded %s (%d bytes)\n", fontPath, _fontDataSize);
                        _ofr.setCacheSize(20, 20, 65536);
                        return true;
                    } else {
                        Serial.printf("FontMgr: FreeType error %d loading %s\n", err, fontPath);
                        free(_fontData);
                        _fontData = nullptr;
                    }
                }
                f.close();
            }
        }
        
        // Try SystemFS
        if (SystemFS.totalBytes() > 0 && SystemFS.exists(fontPath)) {
            Serial.printf("FontMgr: Found %s in SystemFS\n", fontPath);
            File f = SystemFS.open(fontPath, "r");
            if (f) {
                _fontDataSize = f.size();
                _fontData = (uint8_t*)ps_malloc(_fontDataSize);
                if (!_fontData) _fontData = (uint8_t*)malloc(_fontDataSize);
                
                if (_fontData) {
                    f.read(_fontData, _fontDataSize);
                    f.close();
                    
                    FT_Error err = _ofr.loadFont(_fontData, _fontDataSize);
                    if (err == 0) {
                        Serial.printf("FontMgr: Loaded %s (%d bytes)\n", fontPath, _fontDataSize);
                        _ofr.setCacheSize(20, 20, 65536);
                        return true;
                    } else {
                        free(_fontData);
                        _fontData = nullptr;
                    }
                }
                f.close();
            }
        }
    }
    
    return false;
}

bool FontMgr::init() {
    Serial.println("FontMgr: Initializing...");
    
    _fontLoaded = loadFontFromFS();
    
    if (_fontLoaded) {
        Serial.println("FontMgr: ✓ TTF font ready for system-wide use");
    } else {
        Serial.println("FontMgr: ✗ No TTF font found, using bitmap fallback");
    }
    
    return _fontLoaded;
}

void FontMgr::drawText(Book32Display& display, const char* text, int x, int y, int fontSize, uint16_t color) {
    if (_fontLoaded) {
        _ofr.setDrawer(display);
        _ofr.setFontSize(fontSize);
        _ofr.setFontColor(color);
        _ofr.setCursor(x, y);
        _ofr.printf("%s", text);
    } else {
        // Fallback to bitmap font
        display.setTextColor(color);
        // Map fontSize to bitmap text size (approximate)
        int textSize = 1;
        if (fontSize >= 28) textSize = 3;
        else if (fontSize >= 20) textSize = 2;
        display.setTextSize(textSize);
        display.setCursor(x, y);
        display.print(text);
    }
}

void FontMgr::drawTextCentered(Book32Display& display, const char* text, int y, int fontSize, uint16_t color) {
    int width = getTextWidth(text, fontSize);
    int x = (display.width() - width) / 2;
    drawText(display, text, x, y, fontSize, color);
}

void FontMgr::drawTextRight(Book32Display& display, const char* text, int x, int y, int fontSize, uint16_t color) {
    int width = getTextWidth(text, fontSize);
    drawText(display, text, x - width, y, fontSize, color);
}

int FontMgr::getTextWidth(const char* text, int fontSize) {
    if (_fontLoaded) {
        _ofr.setFontSize(fontSize);
        return _ofr.getTextWidth(text);
    } else {
        // Approximate for bitmap font
        int textSize = 1;
        if (fontSize >= 28) textSize = 3;
        else if (fontSize >= 20) textSize = 2;
        return strlen(text) * 6 * textSize;
    }
}

int FontMgr::getTextHeight(int fontSize) {
    if (_fontLoaded) {
        return fontSize;  // TTF fonts are sized in pixels
    } else {
        int textSize = 1;
        if (fontSize >= 28) textSize = 3;
        else if (fontSize >= 20) textSize = 2;
        return 8 * textSize;
    }
}
