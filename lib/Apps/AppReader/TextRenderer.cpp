#include "TextRenderer.h"

TextRenderer::TextRenderer(int width, int height, int fontSize) {
    _width = width;
    _height = height;
    _fontSize = fontSize;
    _fontLoaded = false;
    calculateDimensions();
}

bool TextRenderer::loadFont(const uint8_t* data, size_t size) {
    Serial.printf("TextRenderer: Loading font, size=%d bytes\n", size);
    
    // Verify the font data looks like a TTF file
    if (size < 12) {
        Serial.println("TextRenderer: Font data too small");
        return false;
    }
    
    uint32_t magic = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    Serial.printf("TextRenderer: Font magic: 0x%08X ", magic);
    
    if (magic == 0x00010000) {
        Serial.println("(TrueType)");
    } else if (magic == 0x4F54544F) {
        Serial.println("(OpenType/CFF)");
    } else if (magic == 0x74727565) {
        Serial.println("(TrueType 'true')");
    } else {
        Serial.println("(UNKNOWN - may not be a font!)");
    }
    
    // OpenFontRender::loadFont returns FT_Error (0 = success, non-zero = error)
    // CRITICAL: FT_Error 0 means SUCCESS, so we must check == 0, not use as bool!
    FT_Error err = _ofr.loadFont(data, size);
    Serial.printf("TextRenderer: FreeType returned error code: %d\n", err);
    
    if (err == 0) {  // 0 = FT_Err_Ok = SUCCESS
        _fontLoaded = true;
        _ofr.setCacheSize(20, 20, 65536);
        calculateDimensions();
        Serial.println("TextRenderer: ✓ Font loaded successfully!");
        return true;
    }
    
    // Common FreeType errors:
    // 1 = cannot open resource, 2 = unknown file format, 6 = invalid argument
    // 85 = invalid face handle, 134 = unimplemented feature
    Serial.printf("TextRenderer: ✗ Font load FAILED (FT_Error %d)\n", err);
    return false;
}

void TextRenderer::calculateDimensions() {
    if (_fontLoaded) {
        _ofr.setFontSize(_fontSize);
        int usableWidth = _width - 50;  // Smaller margins
        int avgCharWidth = _ofr.getTextWidth("n");
        _charsPerLine = usableWidth / (avgCharWidth > 0 ? avgCharWidth : (_fontSize/2));
        _lineHeight = _fontSize + 4;  // Tighter line spacing (1.2x)
        _linesPerPage = (_height - 70) / _lineHeight;  // More usable space
    } else {
        int charWidth = 10;
        _charsPerLine = (_width - 50) / charWidth;
        _lineHeight = 20;
        _linesPerPage = (_height - 70) / _lineHeight;
    }
    Serial.printf("TextRenderer: %dx%d font=%dpx lines=%d lpp=%d\n", _width, _height, _fontSize, _lineHeight, _linesPerPage);
}

std::vector<String> TextRenderer::wrapText(const String& text) {
    std::vector<String> lines;
    int pos = 0;
    int textLen = text.length();
    int usableWidth = _width - 60;

    while (pos < textLen) {
        int newlinePos = text.indexOf('\n', pos);
        int endPos = (newlinePos != -1) ? newlinePos : textLen;
        String paragraph = text.substring(pos, endPos);
        
        if (paragraph.length() == 0) {
            lines.push_back("");
        } else {
            int start = 0;
            while (start < paragraph.length()) {
                int len = 1;
                if (_fontLoaded) {
                    while (start + len <= paragraph.length()) {
                        String test = paragraph.substring(start, start + len);
                        if (_ofr.getTextWidth(test.c_str()) > usableWidth) { len--; break; }
                        len++;
                    }
                    if (start + len < paragraph.length()) {
                        int lastSpace = paragraph.lastIndexOf(' ', start + len);
                        if (lastSpace > start) len = lastSpace - start + 1;
                    }
                } else {
                    len = _charsPerLine;
                    if (start + len < paragraph.length()) {
                        int lastSpace = paragraph.lastIndexOf(' ', start + len);
                        if (lastSpace > start) len = lastSpace - start + 1;
                    }
                }
                String line = paragraph.substring(start, start + len);
                line.trim();
                lines.push_back(line);
                start += len;
            }
        }
        if (newlinePos != -1) pos = newlinePos + 1;
        else break;
        yield();
    }
    return lines;
}

std::vector<String> TextRenderer::paginate(const String& text) {
    std::vector<String> pages;
    std::vector<String> lines = wrapText(text);
    String currentPage = "";
    int linesInPage = 0;
    for (const auto& line : lines) {
        if (linesInPage >= _linesPerPage) { pages.push_back(currentPage); currentPage = ""; linesInPage = 0; }
        currentPage += line + "\n";
        linesInPage++;
    }
    if (currentPage.length() > 0) pages.push_back(currentPage);
    return pages;
}

void TextRenderer::renderPage(Book32Display& display, const String& pageText, int pageNum, int totalPages) {
    int x = 25;
    int y = 35;  // Start higher
    int maxY = _height - 50;
    
    if (_fontLoaded) {
        _ofr.setDrawer(display);
        _ofr.setFontColor(GxEPD_BLACK);
        _ofr.setFontSize(_fontSize);
        int lineStart = 0;
        while (lineStart < (int)pageText.length() && y < maxY) {
            int lineEnd = pageText.indexOf('\n', lineStart);
            if (lineEnd == -1) lineEnd = pageText.length();
            _ofr.setCursor(x, y);
            _ofr.printf("%s", pageText.substring(lineStart, lineEnd).c_str());
            y += _lineHeight;
            lineStart = lineEnd + 1;
        }
    } else {
        display.setTextColor(GxEPD_BLACK);
        display.setTextSize(2);
        int lineStart = 0;
        while (lineStart < (int)pageText.length() && y < maxY) {
            int lineEnd = pageText.indexOf('\n', lineStart);
            if (lineEnd == -1) lineEnd = pageText.length();
            display.setCursor(x, y);
            display.print(pageText.substring(lineStart, lineEnd));
            y += _lineHeight;
            lineStart = lineEnd + 1;
        }
    }
    
    // Footer - page number centered at bottom
    if (_fontLoaded) {
        char footer[32];
        snprintf(footer, sizeof(footer), "Page %d of %d", pageNum + 1, totalPages);
        int footerWidth = _ofr.getTextWidth(footer);
        _ofr.setFontSize(14);  // Smaller footer
        _ofr.setCursor((_width - footerWidth) / 2, _height - 20);
        _ofr.printf("%s", footer);
    } else {
        display.setTextSize(1);
        String footer = "Page " + String(pageNum + 1) + " of " + String(totalPages);
        display.setCursor((_width - footer.length()*6)/2, _height - 20);
        display.print(footer);
    }
}

void TextRenderer::renderTextNode(Book32Display& display, RichTextNode& node, int& y, int maxY) {
    if (y >= maxY) return;
    
    // Determine font size based on style
    int fontSize = _fontSize;
    bool isBold = false;
    bool isItalic = false;
    
    if (node.style == STYLE_HEADER1) { fontSize = _fontSize + 6; isBold = true; }
    else if (node.style == STYLE_HEADER2) { fontSize = _fontSize + 4; isBold = true; }
    else if (node.style == STYLE_HEADER3) { fontSize = _fontSize + 2; isBold = true; }
    else if (node.style == STYLE_BOLD) { isBold = true; }
    else if (node.style == STYLE_ITALIC) { isItalic = true; }
    else if (node.style == STYLE_BOLD_ITALIC) { isBold = true; isItalic = true; }
    
    int x_margin = 25;
    int usableWidth = _width - (x_margin * 2);
    int lineSpacing = fontSize + 4;
    
    if (_fontLoaded) {
        _ofr.setDrawer(display);
        _ofr.setFontSize(fontSize);
        _ofr.setFontColor(GxEPD_BLACK);
        
        int currentX = x_margin;
        // Paragraph indent for normal text (first line of long paragraphs)
        bool doIndent = (node.style == STYLE_NORMAL && !node.isListItem && node.text.length() > 50);
        if (doIndent) currentX += 20;
        
        String text = node.text;
        if (node.isListItem) text = "• " + text;
        
        int pos = 0;
        bool firstLine = true;
        while(pos < (int)text.length() && y < maxY) {
            String line = "";
            int line_width = 0;
            
            // Word wrap
            while(pos < (int)text.length()) {
                int next_space = text.indexOf(' ', pos);
                if (next_space == -1) next_space = text.length();
                String word = text.substring(pos, next_space);
                if (line.length() > 0) word = " " + word;
                int word_width = _ofr.getTextWidth(word.c_str());
                
                int availWidth = usableWidth - (currentX - x_margin);
                if (line_width + word_width > availWidth && line.length() > 0) break;
                
                line += word;
                line_width += word_width;
                pos = next_space;
                if (pos < (int)text.length() && text.charAt(pos) == ' ') pos++;
            }
            
            // Calculate draw position
            int drawX = currentX;
            if (node.align == ALIGN_CENTER) drawX = (_width - line_width) / 2;
            else if (node.align == ALIGN_RIGHT) drawX = _width - line_width - x_margin;
            
            // Draw the line (with faux bold if needed)
            _ofr.setCursor(drawX, y);
            _ofr.printf("%s", line.c_str());
            
            // Faux bold: draw again with 1px offset
            if (isBold) {
                _ofr.setCursor(drawX + 1, y);
                _ofr.printf("%s", line.c_str());
            }
            
            y += lineSpacing;
            currentX = x_margin;  // Reset indent after first line
            firstLine = false;
        }
        
        // Small gap after long paragraphs only
        if (node.text.length() > 50) {
            y += 4;
        }
    } else {
        // Bitmap fallback
        display.setTextSize(2);
        String text = node.text;
        if (node.isListItem) text = "• " + text;
        int charWidth = 10;
        int charsPerLine = usableWidth / charWidth;
        int pos = 0;
        while(pos < (int)text.length() && y < maxY) {
            int len = charsPerLine;
            if (pos + len < (int)text.length()) {
                int lastSpace = text.lastIndexOf(' ', pos + len);
                if (lastSpace > pos) len = lastSpace - pos + 1;
            }
            String line = text.substring(pos, pos + len);
            display.setCursor(x_margin, y);
            display.print(line);
            y += 20;
            pos += len;
        }
    }
}

void TextRenderer::renderTable(Book32Display& display, Table& table, int& y, int maxY) {
    display.drawRect(30, y, _width - 60, 40, GxEPD_BLACK);
    y += 50;
}

std::vector<String> TextRenderer::paginateRich(std::vector<ContentNode>& content) {
    std::vector<String> pages;
    String currentPage = "";
    int currentY = 30;
    int maxY = _height - 40;  // More usable space
    
    Serial.printf("Paginating: screen=%dx%d maxY=%d\n", _width, _height, maxY);
    
    for (auto& node : content) {
        if (node.type == CONTENT_TEXT) {
            String serialized = "T:" + String((int)node.textNode.style) + ":" + String((int)node.textNode.align) + ":" + String(node.textNode.isListItem ? "1" : "0") + ":" + node.textNode.text + "\n";
            
            // Calculate height this node will take
            int fontSize = _fontSize;
            if (node.textNode.style == STYLE_HEADER1) fontSize += 6;
            else if (node.textNode.style == STYLE_HEADER2) fontSize += 4;
            else if (node.textNode.style == STYLE_HEADER3) fontSize += 2;
            
            int lineSpacing = fontSize + 4;
            int usableWidth = _width - 50;
            
            // More accurate width estimation for proportional fonts
            int avgCharWidth = _fontLoaded ? 8 : 10;  // ~8px average for 18px font
            int charsPerLine = usableWidth / avgCharWidth;
            if (charsPerLine < 20) charsPerLine = 20;
            
            int textLen = node.textNode.text.length();
            int textLines = (textLen + charsPerLine - 1) / charsPerLine;  // Ceiling division
            if (textLines < 1) textLines = 1;
            
            // Only add paragraph spacing for actual paragraphs, not short lines
            int nodeHeight = textLines * lineSpacing;
            if (textLen > 50) nodeHeight += 6;  // Small gap after paragraphs
            
            // Check if we need a new page
            if (currentY + nodeHeight > maxY && currentPage.length() > 0) {
                pages.push_back(currentPage);
                currentPage = "";
                currentY = 30;
            }
            
            currentPage += serialized;
            currentY += nodeHeight;
        }
        yield();
    }
    
    if (currentPage.length() > 0) pages.push_back(currentPage);
    Serial.printf("Paginated into %d pages\n", pages.size());
    return pages;
}

RenderResult TextRenderer::renderRichPageDynamic(Book32Display& display, const std::vector<ContentNode>& content, 
                                                 int startNode, int startOffset, int pageNum, int totalPages, bool draw) {
    int y = 30;
    int maxY = _height - 35;
    RenderResult result = {0, 0, false};
    
    if (draw) {
        Serial.printf("Dynamic Rendering page %d: startNode=%d startOffset=%d\n", pageNum+1, startNode, startOffset);
        _ofr.setDrawer(display);
        _ofr.setFontColor(GxEPD_BLACK);
    }
    
    int currentNode = startNode;
    int currentOffset = startOffset;
    
    while (currentNode < (int)content.size() && y < maxY) {
        yield(); // Prevent WDT reset
        auto& node = content[currentNode];
        if (node.type == CONTENT_TEXT) {
            if (draw) {
                Serial.printf("  Rendering node %d (type %d) at y=%d\n", currentNode, node.textNode.style, y);
            }
            // Setup for this specific node
            int fontSize = _fontSize;
            bool isBold = false;
            if (node.textNode.style == STYLE_HEADER1) { fontSize = _fontSize + 6; isBold = true; }
            else if (node.textNode.style == STYLE_HEADER2) { fontSize = _fontSize + 4; isBold = true; }
            else if (node.textNode.style == STYLE_HEADER3) { fontSize = _fontSize + 2; isBold = true; }
            else if (node.textNode.style == STYLE_BOLD) { isBold = true; }
            else if (node.textNode.style == STYLE_BOLD_ITALIC) { isBold = true; }
            
            int x_margin = 25;
            int usableWidth = _width - (x_margin * 2);
            int lineSpacing = fontSize + 4;
            
            String text = node.textNode.text.substring(currentOffset);
            if (node.textNode.isListItem && currentOffset == 0) text = "• " + text;
            int pos = 0;

            if (_fontLoaded) {
                if (draw) _ofr.setFontSize(fontSize);
                
                while (pos < (int)text.length()) {
                    // Peek if next line fits
                    if (y + lineSpacing > maxY) {
                        result.pageFull = true;
                        result.charsConsumedInLastNode = currentOffset + pos;
                        return result;
                    }
                    
                    String line = "";
                    int line_width = 0;
                    int line_chars = 0;
                    
                    // Word wrap
                    while (pos + line_chars < (int)text.length()) {
                        int next_space = text.indexOf(' ', pos + line_chars);
                        if (next_space == -1) next_space = text.length();
                        
                        String word = text.substring(pos + line_chars, next_space);
                        if (line_chars > 0) word = " " + word;
                        
                        int word_width = _ofr.getTextWidth(word.c_str());
                        if (line_width + word_width > usableWidth && line_chars > 0) break;
                        
                        line += word;
                        line_width += word_width;
                        line_chars = next_space - pos;
                        if (pos + line_chars < (int)text.length() && text.charAt(pos + line_chars) == ' ') line_chars++;
                    }
                    
                    if (draw) {
                        int drawX = x_margin;
                        if (node.textNode.align == ALIGN_CENTER) drawX = (_width - line_width) / 2;
                        else if (node.textNode.align == ALIGN_RIGHT) drawX = _width - line_width - x_margin;
                        
                        _ofr.setCursor(drawX, y);
                        _ofr.printf("%s", line.c_str());
                        if (isBold) {
                            _ofr.setCursor(drawX + 1, y);
                            _ofr.printf("%s", line.c_str());
                        }
                    }
                    
                    y += lineSpacing;
                    pos += line_chars;
                }
                // Small gap after paragraph
                if (y + 6 < maxY) y += 6;
            } else {
                // Bitmap fallback (simplified)
                display.setCursor(x_margin, y);
                display.print(text);
                y += 20; 
                pos = text.length(); 
            }
        }
        
        currentNode++;
        currentOffset = 0; // Reset for next node
        result.nodesConsumed++;
    }
    
    if (draw) {
        // Footer
        if (_fontLoaded) {
            char footer[32];
            snprintf(footer, sizeof(footer), "Page %d of %d", pageNum + 1, totalPages);
            _ofr.setFontSize(12);
            int footerWidth = _ofr.getTextWidth(footer);
            _ofr.setCursor((_width - footerWidth) / 2, _height - 15);
            _ofr.printf("%s", footer);
        }
    }
    
    return result;
}
