#include "TextRenderer.h"

TextRenderer::TextRenderer(int width, int height, int fontSize) {
    _width = width;
    _height = height;
    _fontSize = fontSize;
    _fontLoaded = true;
    _cachedPage = -1;
    _lastGFXFont = nullptr;
    memset(_gfxCharWidths, 0, sizeof(_gfxCharWidths));
    calculateDimensions();
}

bool TextRenderer::loadFont(const uint8_t* data, size_t size) {
    return true; // GFX fonts are compiled in
}

void TextRenderer::calculateDimensions() {
    _lineHeight = 24; 
    _linesPerPage = (_height - 70) / _lineHeight;
}

const GFXfont* TextRenderer::getGFXFont(TextStyle style, int& lineHeight) {
    if (style == STYLE_HEADER1) { lineHeight = 36; return &FreeSansBold18pt7b; }
    if (style == STYLE_HEADER2) { lineHeight = 28; return &FreeSansBold12pt7b; }
    if (style == STYLE_HEADER3) { lineHeight = 24; return &FreeSansBold9pt7b; }
    if (style == STYLE_BOLD)    { lineHeight = 24; return &FreeSansBold9pt7b; }
    
    lineHeight = 24; // Default for 9pt Sans
    return &FreeSans9pt7b;
}

RenderResult TextRenderer::renderRichPageDynamic(Book32Display& display, const std::vector<ContentNode>& content, 
                                                 int startNode, int startOffset, int pageNum, int totalPages, bool draw) {
    
    if (draw && _cachedPage == pageNum && !_lineCache.empty()) {
        display.setTextColor(GxEPD_BLACK);
        for (const auto& line : _lineCache) {
            int unused;
            display.setFont(getGFXFont((TextStyle)line.fontSize, unused)); 
            display.setCursor(line.x, line.y);
            display.print(line.text);
        }
        display.setFont(NULL);
        display.setCursor(_width/2 - 20, _height - 15);
        display.printf("Page %d", pageNum + 1);
        return {0, 0, true};
    }

    _lineCache.clear();
    _cachedPage = pageNum;

    int y = 40; 
    int maxY = _height - 40;
    RenderResult result = {0, 0, false};
    int currentNode = startNode;
    int currentOffset = startOffset;
    
    char lineBuf[256];

    while (currentNode < (int)content.size() && y < maxY) {
        auto& node = content[currentNode];
        if (node.type == CONTENT_TEXT) {
            int lineSpacing = 0;
            const GFXfont* font = getGFXFont(node.textNode.style, lineSpacing);
            display.setFont(font);
            
            // Re-cache character widths if font changed
            if (font != _lastGFXFont) {
                for (uint8_t c = 32; c < 127; c++) {
                    if (c >= font->first && c <= font->last) {
                        GFXglyph *glyph = &(font->glyph[c - font->first]);
                        _gfxCharWidths[c] = glyph->xAdvance;
                    } else {
                        _gfxCharWidths[c] = 0;
                    }
                }
                _lastGFXFont = font;
            }

            int x_margin = 40;
            int usableWidth = _width - (x_margin * 2);

            const char* text = node.textNode.text.c_str();
            int textLen = node.textNode.text.length();
            int pos = currentOffset;

            while (pos < textLen && y < maxY) {
                if (y + lineSpacing > maxY) {
                    result.pageFull = true;
                    result.charsConsumedInLastNode = pos;
                    return result;
                }

                lineBuf[0] = '\0';
                int line_width = 0;
                int line_chars = 0;
                
                if (node.textNode.isListItem && pos == currentOffset) {
                    strcpy(lineBuf, "- ");
                    line_width = _gfxCharWidths['-'] + _gfxCharWidths[' '];
                }

                while (pos + line_chars < textLen) {
                    int wordStart = pos + line_chars;
                    while (wordStart < textLen && isspace((unsigned char)text[wordStart])) wordStart++;
                    if (wordStart >= textLen) { line_chars = textLen - pos; break; }

                    int wordEnd = wordStart;
                    while (wordEnd < textLen && !isspace((unsigned char)text[wordEnd])) wordEnd++;
                    
                    // Ultra-fast width measurement
                    int wordWidth = 0;
                    for (int k = wordStart; k < wordEnd; k++) {
                        unsigned char c = (unsigned char)text[k];
                        if (c < 128) wordWidth += _gfxCharWidths[c];
                        else wordWidth += 8; // Fallback for unicode
                    }

                    int spaceWidth = (line_width > 0) ? _gfxCharWidths[' '] : 0;

                    if (line_width + spaceWidth + wordWidth > usableWidth && line_width > 0) break;

                    if (line_width > 0) {
                        strcat(lineBuf, " ");
                        line_width += spaceWidth;
                    }
                    
                    int currentLen = strlen(lineBuf);
                    int toCopy = wordEnd - wordStart;
                    if (currentLen + toCopy < 255) {
                        strncat(lineBuf, text + wordStart, toCopy);
                        line_width += wordWidth;
                    }
                    line_chars = wordEnd - pos;
                }

                if (strlen(lineBuf) > 0) {
                    int drawX = x_margin;
                    if (node.textNode.align == ALIGN_CENTER) drawX = (_width - line_width) / 2;
                    else if (node.textNode.align == ALIGN_RIGHT) drawX = _width - line_width - x_margin;
                    
                    _lineCache.push_back({drawX, y, (int)node.textNode.style, false, String(lineBuf)});
                    if (draw) {
                        display.setCursor(drawX, y);
                        display.print(lineBuf);
                    }
                    y += lineSpacing;
                } else {
                    pos++; continue;
                }
                
                pos += line_chars;
                if (pos < textLen && isspace((unsigned char)text[pos])) pos++; 
                yield();
            }
            if (y + 10 < maxY) y += 10;
        }
        currentNode++;
        currentOffset = 0;
        result.nodesConsumed++;
    }

    if (draw) {
        display.setFont(NULL);
        display.setCursor(_width/2 - 20, _height - 15);
        display.printf("Page %d", pageNum + 1);
    }
    return result;
}

std::vector<String> TextRenderer::wrapText(const String& text) { return std::vector<String>(); }
std::vector<String> TextRenderer::paginate(const String& text) { return std::vector<String>(); }
void TextRenderer::renderPage(Book32Display& display, const String& pageText, int pageNum, int totalPages) {}
std::vector<String> TextRenderer::paginateRich(std::vector<ContentNode>& content) { return std::vector<String>(); }
void TextRenderer::renderRichPage(Book32Display& display, const String& pageData, int pageNum, int totalPages) {}
void TextRenderer::renderTextNode(Book32Display& display, RichTextNode& node, int& y, int maxY) {}
void TextRenderer::renderTable(Book32Display& display, Table& table, int& y, int maxY) {}
