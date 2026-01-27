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
    return true; 
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
    
    lineHeight = 24; 
    return &FreeSans9pt7b;
}

RenderResult TextRenderer::renderRichPageDynamic(Book32Display& display, const std::vector<ContentNode>& content, 
                                                 int startNode, int startOffset, int pageNum, int totalPages, bool draw) {
    if (draw) {
        display.setTextColor(GxEPD_BLACK);
    }

    if (draw && _cachedPage == pageNum && !_lineCache.empty()) {
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
    int line_width = 0;
    int x_margin = 35;
    int currentX = x_margin;

    while (currentNode < (int)content.size() && y < maxY) {
        auto& node = content[currentNode];
        if (node.type == CONTENT_TEXT) {
            int nodeLineHeight = 0;
            const GFXfont* font = getGFXFont(node.textNode.style, nodeLineHeight);
            display.setFont(font);
            
            if (font != _lastGFXFont) {
                for (uint8_t c = 32; c < 127; c++) {
                    if (c >= font->first && c <= font->last) {
                        _gfxCharWidths[c] = font->glyph[c - font->first].xAdvance;
                    } else {
                        _gfxCharWidths[c] = 0;
                    }
                }
                _lastGFXFont = font;
            }

            if (node.textNode.isBlockStart && currentOffset == 0) {
                if (line_width > 0) { y += nodeLineHeight; line_width = 0; }
                currentX = x_margin + node.textNode.indent;
            }

            const char* text = node.textNode.text.c_str();
            int textLen = node.textNode.text.length();
            int pos = currentOffset;

            while (pos < textLen && y < maxY) {
                int line_chars = 0;
                lineBuf[0] = '\0';
                int segment_width = 0;
                
                if (node.textNode.isListItem && pos == currentOffset) {
                    strcpy(lineBuf, "- ");
                    segment_width = _gfxCharWidths['-'] + _gfxCharWidths[' '];
                }

                while (pos + line_chars < textLen) {
                    int wordStart = pos + line_chars;
                    while (wordStart < textLen && isspace((unsigned char)text[wordStart])) wordStart++;
                    if (wordStart >= textLen) { line_chars = textLen - pos; break; }

                    int wordEnd = wordStart;
                    while (wordEnd < textLen && !isspace((unsigned char)text[wordEnd])) wordEnd++;
                    
                    int wordWidth = 0;
                    for (int k = wordStart; k < wordEnd; k++) {
                        unsigned char c = (unsigned char)text[k];
                        wordWidth += (c < 128) ? _gfxCharWidths[c] : 8;
                    }

                    int spaceWidth = (line_width + segment_width > 0) ? _gfxCharWidths[' '] : 0;
                    int usableWidth = _width - x_margin;

                    if (currentX + line_width + segment_width + spaceWidth + wordWidth > usableWidth && (line_width + segment_width) > 0) {
                        // Word doesn't fit on this line
                        if (y + nodeLineHeight > maxY) {
                            result.pageFull = true;
                            result.charsConsumedInLastNode = pos;
                            return result;
                        }
                        
                        // Commit current segment before starting new line
                        if (segment_width > 0) {
                            _lineCache.push_back({currentX + line_width, y, (int)node.textNode.style, false, String(lineBuf)});
                            if (draw) { display.setCursor(currentX + line_width, y); display.print(lineBuf); }
                        }
                        
                        y += nodeLineHeight;
                        line_width = 0;
                        currentX = x_margin;
                        segment_width = 0;
                        lineBuf[0] = '\0';
                        
                        // Retest the word on the new line
                        spaceWidth = 0; 
                    }

                    if (segment_width > 0 || line_width > 0) {
                        strcat(lineBuf, " ");
                        segment_width += spaceWidth;
                    }
                    strncat(lineBuf, text + wordStart, wordEnd - wordStart);
                    segment_width += wordWidth;
                    line_chars = wordEnd - pos;
                }

                if (strlen(lineBuf) > 0) {
                    _lineCache.push_back({currentX + line_width, y, (int)node.textNode.style, false, String(lineBuf)});
                    if (draw) { display.setCursor(currentX + line_width, y); display.print(lineBuf); }
                    line_width += segment_width;
                }
                
                pos += line_chars;
                if (pos < textLen) {
                    // We filled the line but the node has more text
                    y += nodeLineHeight;
                    line_width = 0;
                    currentX = x_margin;
                }
                yield();
            }
            if (node.textNode.isBlockStart && currentNode < (int)content.size() - 1 && content[currentNode+1].textNode.isBlockStart) {
                y += 8; // Paragraph gap
            }
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
