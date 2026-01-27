#include "TextRenderer.h"

TextRenderer::TextRenderer(int width, int height, int fontSize) {
    _width = width;
    _height = height;
    _fontSize = fontSize;
    _fontLoaded = false;
    _cachedPage = -1;
    _lastFontSize = -1;
    memset(_charWidths, 0, sizeof(_charWidths));
    calculateDimensions();
}

bool TextRenderer::loadFont(const uint8_t* data, size_t size) {
    FT_Error err = _ofr.loadFont(data, size);
    if (err == 0) {
        _fontLoaded = true;
        _ofr.setCacheSize(20, 20, 65536);
        calculateDimensions();
        return true;
    }
    return false;
}

void TextRenderer::calculateDimensions() {
    if (_fontLoaded) {
        _ofr.setFontSize(_fontSize);
        int usableWidth = _width - 50;
        int avgCharWidth = _ofr.getTextWidth("n");
        _charsPerLine = usableWidth / (avgCharWidth > 0 ? avgCharWidth : (_fontSize/2));
        _lineHeight = _fontSize + 4;
        _linesPerPage = (_height - 70) / _lineHeight;
    } else {
        _charsPerLine = (_width - 50) / 10;
        _lineHeight = 20;
        _linesPerPage = (_height - 70) / _lineHeight;
    }
}

RenderResult TextRenderer::renderRichPageDynamic(Book32Display& display, const std::vector<ContentNode>& content, 
                                                 int startNode, int startOffset, int pageNum, int totalPages, bool draw) {
    if (draw) {
        _ofr.setDrawer(display);
        _ofr.setFontColor(GxEPD_BLACK);
    }

    if (draw && _cachedPage == pageNum && !_lineCache.empty()) {
        for (const auto& line : _lineCache) {
            _ofr.setFontSize(line.fontSize);
            _ofr.setCursor(line.x, line.y);
            _ofr.printf("%s", line.text.c_str());
            if (line.isBold) {
                _ofr.setCursor(line.x + 1, line.y);
                _ofr.printf("%s", line.text.c_str());
            }
        }
        char footer[32];
        snprintf(footer, sizeof(footer), "Page %d", pageNum + 1);
        _ofr.setFontSize(12);
        int footerWidth = _ofr.getTextWidth(footer);
        _ofr.setCursor((_width - footerWidth) / 2, _height - 15);
        _ofr.printf("%s", footer);
        return {0, 0, true};
    }

    _lineCache.clear();
    _cachedPage = pageNum;

    int y = 30;
    int maxY = _height - 40;
    RenderResult result = {0, 0, false};
    int currentNode = startNode;
    int currentOffset = startOffset;
    
    char lineBuf[256];

    while (currentNode < (int)content.size() && y < maxY) {
        auto& node = content[currentNode];
        if (node.type == CONTENT_TEXT) {
            int fontSize = _fontSize;
            bool isBold = false;
            if (node.textNode.style == STYLE_HEADER1) { fontSize = _fontSize + 12; isBold = true; }
            else if (node.textNode.style == STYLE_HEADER2) { fontSize = _fontSize + 8; isBold = true; }
            else if (node.textNode.style == STYLE_HEADER3) { fontSize = _fontSize + 4; isBold = true; }
            else if (node.textNode.style == STYLE_BOLD) { isBold = true; }
            
            _ofr.setFontSize(fontSize);
            int lineSpacing = fontSize + 8;
            int x_margin = 35;
            int usableWidth = _width - (x_margin * 2);

            if (_fontLoaded && fontSize != _lastFontSize) {
                for (int c = 32; c < 127; c++) {
                    char buf[2] = {(char)c, 0};
                    _charWidths[c] = _ofr.getTextWidth(buf);
                }
                _lastFontSize = fontSize;
            }

            const char* text = node.textNode.text.c_str();
            int textLen = node.textNode.text.length();
            int pos = currentOffset;

            while (pos < textLen && y < maxY) {
                if (y + lineSpacing > maxY) {
                    result.pageFull = true;
                    result.charsConsumedInLastNode = pos;
                    return result;
                }

                int line_chars = 0;
                int line_width = 0;
                lineBuf[0] = '\0';
                
                if (node.textNode.isListItem && pos == currentOffset) {
                    strcpy(lineBuf, "• ");
                    line_width = _fontLoaded ? _charWidths['n'] * 2 : 20; 
                }

                while (pos + line_chars < textLen) {
                    int wordStart = pos + line_chars;
                    while (wordStart < textLen && isspace((unsigned char)text[wordStart])) wordStart++;
                    
                    if (wordStart >= textLen) {
                        line_chars = textLen - pos;
                        break;
                    }

                    int wordEnd = wordStart;
                    while (wordEnd < textLen && !isspace((unsigned char)text[wordEnd])) wordEnd++;
                    
                    int wordWidth = 0;
                    for (int k = wordStart; k < wordEnd; k++) {
                        unsigned char uc = (unsigned char)text[k];
                        if (_fontLoaded && uc < 128 && _charWidths[uc] > 0) wordWidth += _charWidths[uc];
                        else wordWidth += (fontSize / 2);
                    }

                    int spaceWidth = (line_width > 0) ? (_fontLoaded ? _charWidths[' '] : (fontSize/4)) : 0;

                    if (line_width + spaceWidth + wordWidth > usableWidth && line_width > 0) {
                        break;
                    }

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
                    
                    _lineCache.push_back({drawX, y, fontSize, isBold, String(lineBuf)});
                    if (draw) {
                        _ofr.setCursor(drawX, y);
                        _ofr.printf("%s", lineBuf);
                        if (isBold) {
                            _ofr.setCursor(drawX + 1, y);
                            _ofr.printf("%s", lineBuf);
                        }
                    }
                    y += lineSpacing;
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
        char footer[32];
        snprintf(footer, sizeof(footer), "Page %d", pageNum + 1);
        _ofr.setFontSize(12);
        int footerWidth = _ofr.getTextWidth(footer);
        _ofr.setCursor((_width - footerWidth) / 2, _height - 15);
        _ofr.printf("%s", footer);
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
