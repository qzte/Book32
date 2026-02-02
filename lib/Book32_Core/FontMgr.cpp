#include "FontMgr.h"

FontMgr::FontMgr() {
    memset(_charWidths, 0, sizeof(_charWidths));
}

FontMgr::~FontMgr() {}

FontMgr& FontMgr::getInstance() {
    static FontMgr instance;
    return instance;
}

bool FontMgr::init() {
    Serial.println("FontMgr: Initialized with Adafruit GFX FreeSans fonts");
    return true;
}

const GFXfont* FontMgr::getFont(int fontSize) {
    if (fontSize >= 30) return &FreeSans24pt7b;
    if (fontSize >= 22) return &FreeSans18pt7b;
    if (fontSize >= 16) return &FreeSans12pt7b;
    return &FreeSans9pt7b;
}

const GFXfont* FontMgr::getFontBold(int fontSize) {
    if (fontSize >= 30) return &FreeSansBold24pt7b;
    if (fontSize >= 22) return &FreeSansBold18pt7b;
    if (fontSize >= 16) return &FreeSansBold12pt7b;
    return &FreeSansBold9pt7b;
}

void FontMgr::cacheCharWidths(const GFXfont* font) {
    if (font == _lastFont) return;
    _lastFont = font;

    for (uint8_t c = 32; c < 127; c++) {
        if (c >= font->first && c <= font->last) {
            _charWidths[c] = font->glyph[c - font->first].xAdvance;
        } else {
            _charWidths[c] = 0;
        }
    }
}

void FontMgr::drawText(Book32Display& display, const char* text, int x, int y, int fontSize, uint16_t color) {
    const GFXfont* font = getFont(fontSize);
    display.setFont(font);
    display.setTextColor(color);
    display.setCursor(x, y);
    display.print(text);
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
    const GFXfont* font = getFont(fontSize);
    cacheCharWidths(font);

    int width = 0;
    while (*text) {
        unsigned char c = (unsigned char)*text;
        if (c < 128) {
            width += _charWidths[c];
        } else {
            width += 8; // Default width for extended chars
        }
        text++;
    }
    return width;
}

int FontMgr::getTextHeight(int fontSize) {
    const GFXfont* font = getFont(fontSize);
    return font->yAdvance;
}
