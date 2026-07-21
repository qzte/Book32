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
    if (fontSize >= 30) return &FreeSans24pt8b;
    if (fontSize >= 22) return &FreeSans18pt8b;
    if (fontSize >= 16) return &FreeSans12pt8b;
    return &FreeSans9pt8b;
}

const GFXfont* FontMgr::getFontBold(int fontSize) {
    if (fontSize >= 30) return &FreeSansBold24pt8b;
    if (fontSize >= 22) return &FreeSansBold18pt8b;
    if (fontSize >= 16) return &FreeSansBold12pt8b;
    return &FreeSansBold9pt8b;
}

void FontMgr::cacheCharWidths(const GFXfont* font) {
    if (font == _lastFont) return;
    _lastFont = font;

    // int (not uint8_t) loop variable: with an upper bound of 256 a uint8_t
    // would wrap at 255 and never terminate.
    for (int c = 32; c < 256; c++) {
        if (c >= font->first && c <= font->last) {
            _charWidths[c] = font->glyph[c - font->first].xAdvance;
        } else {
            _charWidths[c] = 0;
        }
    }
}

void FontMgr::drawText(Book32Display& display, const char* text, int x, int y, int fontSize, uint16_t color) {
    // Strings arrive as UTF-8 (filenames, EPUB metadata, WiFi SSIDs, ...) but
    // the GFX layer draws one byte per glyph, so convert to Latin-1 here.
    char latin1[512];
    utf8ToLatin1(text, latin1, sizeof(latin1));
    const GFXfont* font = getFont(fontSize);
    display.setFont(font);
    display.setTextColor(color);
    display.setCursor(x, y);
    display.print(latin1);
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

    // Measure over the same Latin-1 bytes drawText() will print, otherwise
    // centered/right-aligned UTF-8 strings are measured on their multi-byte
    // form and drift out of position.
    char latin1[512];
    utf8ToLatin1(text, latin1, sizeof(latin1));

    int width = 0;
    for (const unsigned char* p = (const unsigned char*)latin1; *p; p++) {
        width += _charWidths[*p];
    }
    return width;
}

void FontMgr::utf8ToLatin1(const char* src, char* dst, size_t dstSize) {
    if (dstSize == 0) return;
    size_t o = 0;
    const uint8_t* s = (const uint8_t*)src;
    while (*s && o + 1 < dstSize) {
        uint32_t cp;
        uint8_t b = *s++;
        if (b < 0x80) {
            cp = b;
        } else if ((b & 0xE0) == 0xC0 && (s[0] & 0xC0) == 0x80) {
            cp = ((uint32_t)(b & 0x1F) << 6) | (s[0] & 0x3F);
            s += 1;
        } else if ((b & 0xF0) == 0xE0 && (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80) {
            cp = ((uint32_t)(b & 0x0F) << 12) | ((uint32_t)(s[0] & 0x3F) << 6) | (s[1] & 0x3F);
            s += 2;
        } else if ((b & 0xF8) == 0xF0 && (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
            cp = ((uint32_t)(b & 0x07) << 18) | ((uint32_t)(s[0] & 0x3F) << 12)
               | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
            s += 3;
        } else {
            // Invalid lead byte or orphan continuation byte. Assume the text
            // was already Latin-1 (e.g. pre-converted EPUB content) and pass
            // the byte through untouched.
            dst[o++] = (char)b;
            continue;
        }

        if (cp == 0x00A0) {           // NBSP -> normal space (allows wrapping)
            dst[o++] = ' ';
        } else if (cp == 0x00AD) {    // soft hyphen -> drop
            continue;
        } else if (cp <= 0xFF) {
            dst[o++] = (char)cp;
        } else {
            switch (cp) {
                case 0x2018: case 0x2019: case 0x201A: dst[o++] = '\''; break; // curly single quotes
                case 0x201C: case 0x201D: case 0x201E: dst[o++] = '"';  break; // curly double quotes
                case 0x2013: case 0x2014: dst[o++] = '-'; break;               // en/em dash
                case 0x2026:                                                    // ellipsis
                    dst[o++] = '.';
                    if (o + 1 < dstSize) dst[o++] = '.';
                    if (o + 1 < dstSize) dst[o++] = '.';
                    break;
                default: dst[o++] = '?'; break;
            }
        }
    }
    dst[o] = '\0';
}

String FontMgr::utf8ToLatin1(const String& src) {
    // Latin-1 output is never longer than the UTF-8 input, so a same-sized
    // buffer is always enough.
    size_t bufSize = src.length() + 1;
    char* buf = (char*)malloc(bufSize);
    if (!buf) return src;
    utf8ToLatin1(src.c_str(), buf, bufSize);
    String out(buf);
    free(buf);
    return out;
}

int FontMgr::getTextHeight(int fontSize) {
    const GFXfont* font = getFont(fontSize);
    return font->yAdvance;
}
