#include "TextRenderer.h"

TextRenderer::TextRenderer(int width, int height, int fontSize) {
    _width = width;
    _height = height;
    // Normalize to a supported body size (9/12/18); default to small.
    if (fontSize >= 18) _fontSize = 18;
    else if (fontSize >= 12) _fontSize = 12;
    else _fontSize = 9;
    _fontLoaded = true;
    _cachedPage = -1;
    _lastGFXFont = nullptr;
    memset(_gfxCharWidths, 0, sizeof(_gfxCharWidths));
    calculateDimensions();
}

bool TextRenderer::loadFont(const uint8_t* data, size_t size) {
    return true;
}

void TextRenderer::setFontSize(int size) {
    int normalized = (size >= 18) ? 18 : (size >= 12 ? 12 : 9);
    if (normalized == _fontSize) return;
    _fontSize = normalized;
    // Force width cache + pagination to be recomputed for the new font.
    _lastGFXFont = nullptr;
    clearCache();
    calculateDimensions();
}

void TextRenderer::setFontFamily(int family) {
    int normalized = (family >= READER_FONT_SANS && family <= READER_FONT_GELASIO)
                          ? family : READER_FONT_SANS;
    if (normalized == _fontFamily) return;
    _fontFamily = normalized;
    // Force width cache + pagination to be recomputed for the new font.
    _lastGFXFont = nullptr;
    clearCache();
    calculateDimensions();
}

void TextRenderer::calculateDimensions() {
    int lh = 0;
    getGFXFont(STYLE_NORMAL, lh);
    _lineHeight = lh > 0 ? lh : 24;
    _linesPerPage = (_height - 70) / _lineHeight;
}

void TextRenderer::clearCache() {
    _lineCache.clear();
    _cachedPage = -1;
    _hasCachedResult = false;
}

const GFXfont* TextRenderer::getGFXFont(TextStyle style, int& lineHeight) {
    // Body font follows the user-selected size. Headers step up from the body
    // size (and are always >= body) so the hierarchy holds at every size.
    const GFXfont* normal;
    const GFXfont* bold;
    const GFXfont* h4; const GFXfont* h3; const GFXfont* h2; const GFXfont* h1;

    // Each family provides Regular at 9/12/18pt and Bold at 9/12/18/24pt,
    // mirroring the FreeSans set below so header steps behave identically
    // regardless of which family is selected.
#define B32_FONT_SET(NORMAL9, NORMAL12, NORMAL18, BOLD9, BOLD12, BOLD18, BOLD24) \
    switch (_fontSize) { \
        case 18:  /* Large */ \
            normal = &NORMAL18; bold = &BOLD18; \
            h4 = &BOLD18;       h3 = &BOLD18; \
            h2 = &BOLD24;       h1 = &BOLD24; \
            break; \
        case 12:  /* Medium */ \
            normal = &NORMAL12; bold = &BOLD12; \
            h4 = &BOLD12;       h3 = &BOLD18; \
            h2 = &BOLD18;       h1 = &BOLD24; \
            break; \
        case 9:   /* Small (default) */ \
        default: \
            normal = &NORMAL9;  bold = &BOLD9; \
            h4 = &BOLD9;        h3 = &BOLD12; \
            h2 = &BOLD18;       h1 = &BOLD24; \
            break; \
    }

    switch (_fontFamily) {
        case READER_FONT_MERRIWEATHER:
            B32_FONT_SET(Merriweather_Regular9pt8b, Merriweather_Regular12pt8b, Merriweather_Regular18pt8b,
                         Merriweather_Bold9pt8b, Merriweather_Bold12pt8b, Merriweather_Bold18pt8b, Merriweather_Bold24pt8b)
            break;
        case READER_FONT_LITERATA:
            B32_FONT_SET(Literata_Regular9pt8b, Literata_Regular12pt8b, Literata_Regular18pt8b,
                         Literata_Bold9pt8b, Literata_Bold12pt8b, Literata_Bold18pt8b, Literata_Bold24pt8b)
            break;
        case READER_FONT_SOURCE_SERIF:
            B32_FONT_SET(SourceSerif4_Regular9pt8b, SourceSerif4_Regular12pt8b, SourceSerif4_Regular18pt8b,
                         SourceSerif4_Bold9pt8b, SourceSerif4_Bold12pt8b, SourceSerif4_Bold18pt8b, SourceSerif4_Bold24pt8b)
            break;
        case READER_FONT_GELASIO:
            B32_FONT_SET(Gelasio_Regular9pt8b, Gelasio_Regular12pt8b, Gelasio_Regular18pt8b,
                         Gelasio_Bold9pt8b, Gelasio_Bold12pt8b, Gelasio_Bold18pt8b, Gelasio_Bold24pt8b)
            break;
        case READER_FONT_SANS:
        default:
            B32_FONT_SET(FreeSans9pt8b, FreeSans12pt8b, FreeSans18pt8b,
                         FreeSansBold9pt8b, FreeSansBold12pt8b, FreeSansBold18pt8b, FreeSansBold24pt8b)
            break;
    }
#undef B32_FONT_SET

    const GFXfont* font;
    switch (style) {
        case STYLE_HEADER1: font = h1;   break;
        case STYLE_HEADER2: font = h2;   break;
        case STYLE_HEADER3: font = h3;   break;
        case STYLE_HEADER4: font = h4;   break;
        case STYLE_BOLD:    font = bold; break;
        default:            font = normal; break;
    }

    // Line height = the font's own advance plus a little leading. Deriving it
    // from the font keeps vertical spacing, word-wrap and page breaks correct
    // for any size instead of relying on hand-tuned constants.
    lineHeight = font->yAdvance + 2;
    return font;
}

RenderResult TextRenderer::renderRichPageDynamic(Book32Display& display, const std::vector<ContentNode>& content, 
                                                 int startNode, int startOffset, int pageNum, int pageNumForDisplay, bool draw) {
    if (draw) {
        display.setTextColor(GxEPD_BLACK);
    }

    if (draw && _cachedPage == pageNum && !_lineCache.empty() && _hasCachedResult) {
        for (const auto& line : _lineCache) {
            int unused;
            display.setFont(getGFXFont((TextStyle)line.fontSize, unused)); 
            display.setCursor(line.x, line.y);
            display.print(line.text);
        }
        // Page number drawing moved to AppReader for consistency
        return _cachedResult;
    }

    _lineCache.clear();
    _cachedPage = pageNum;

    int y = 40; 
    int maxY = _height - 40;
    RenderResult result = {0, 0, false, startNode, startOffset};
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
                // int (not uint8_t) loop variable: an upper bound of 256
                // would make a uint8_t wrap at 255 and never terminate.
                for (int c = 32; c < 256; c++) {
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
                
                // Add extra spacing before headers
                if (node.textNode.style == STYLE_HEADER1) {
                    y += 30; // Big gap before chapter title
                    currentX = 0; // Will be centered below
                } else if (node.textNode.style == STYLE_HEADER2) {
                    y += 20;
                    currentX = 0; // Centered
                } else if (node.textNode.style == STYLE_HEADER3) {
                    y += 12;
                }
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
                        wordWidth += _gfxCharWidths[c];
                    }

                    int spaceWidth = (line_width + segment_width > 0) ? _gfxCharWidths[' '] : 0;
                    int usableWidth = _width - x_margin;

                    if (currentX + line_width + segment_width + spaceWidth + wordWidth > usableWidth && (line_width + segment_width) > 0) {
                        // Word doesn't fit on this line
                        if (y + nodeLineHeight > maxY) {
                            if (strlen(lineBuf) > 0) {
                                int drawX = currentX + line_width;
                                if (node.textNode.style == STYLE_HEADER1 || node.textNode.style == STYLE_HEADER2) {
                                    drawX = (_width - segment_width) / 2;
                                }
                                _lineCache.push_back({drawX, y, (int)node.textNode.style, false, String(lineBuf)});
                                if (draw) { display.setCursor(drawX, y); display.print(lineBuf); }
                            }

                            int nextOffset = pos + line_chars;
                            result.pageFull = true;
                            result.charsConsumedInLastNode = nextOffset;
                            result.nextNodeIndex = currentNode;
                            result.nextCharOffset = nextOffset;
                            _cachedResult = result;
                            _hasCachedResult = true;
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
                    int drawX = currentX + line_width;
                    // Center headers (H1, H2)
                    if (node.textNode.style == STYLE_HEADER1 || node.textNode.style == STYLE_HEADER2) {
                        drawX = (_width - segment_width) / 2;
                    }
                    _lineCache.push_back({drawX, y, (int)node.textNode.style, false, String(lineBuf)});
                    if (draw) { display.setCursor(drawX, y); display.print(lineBuf); }
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
            
            // CRITICAL: Check if we exited the text loop because page is full but text remains
            if (pos < textLen && y >= maxY) {
                // Page full but this node has more text - return position in this node
                result.pageFull = true;
                result.charsConsumedInLastNode = pos;
                result.nextNodeIndex = currentNode;
                result.nextCharOffset = pos;
                // nodesConsumed is the count of COMPLETED nodes before this one
                _cachedResult = result;
                _hasCachedResult = true;
                return result;
            }
            
            if (node.textNode.isBlockStart && currentNode < (int)content.size() - 1 && content[currentNode+1].textNode.isBlockStart) {
                y += 8; // Paragraph gap
            }
            // Add extra spacing after headers
            if (node.textNode.style == STYLE_HEADER1) {
                y += 25; // Extra gap after chapter title
            } else if (node.textNode.style == STYLE_HEADER2) {
                y += 15;
            } else if (node.textNode.style == STYLE_HEADER3) {
                y += 10;
            }
        }
        currentNode++;
        currentOffset = 0;
        result.nodesConsumed++;
        result.nextNodeIndex = currentNode;
        result.nextCharOffset = currentOffset;
    }

    // CRITICAL: Check if we stopped because the page is full but there's more content
    // This happens when y >= maxY but we haven't processed all nodes
    if (currentNode < (int)content.size()) {
        // There's still more content to display
        result.pageFull = true;
        result.charsConsumedInLastNode = currentOffset; // Position in current node
        result.nextNodeIndex = currentNode;
        result.nextCharOffset = currentOffset;
        // nodesConsumed already reflects completed nodes
    }
    // If currentNode >= content.size(), all content was displayed -> pageFull stays false (true end of chapter)

    // Page number drawing moved to AppReader for consistency
    _cachedResult = result;
    _hasCachedResult = true;
    return result;
}

std::vector<String> TextRenderer::wrapText(const String& text) { return std::vector<String>(); }
std::vector<String> TextRenderer::paginate(const String& text) { return std::vector<String>(); }
void TextRenderer::renderPage(Book32Display& display, const String& pageText, int pageNum, int totalPages) {}
std::vector<String> TextRenderer::paginateRich(std::vector<ContentNode>& content) { return std::vector<String>(); }
void TextRenderer::renderRichPage(Book32Display& display, const String& pageData, int pageNum, int totalPages) {}
void TextRenderer::renderTextNode(Book32Display& display, RichTextNode& node, int& y, int maxY) {}
void TextRenderer::renderTable(Book32Display& display, Table& table, int& y, int maxY) {}
