#include "TextRenderer.h"

TextRenderer::TextRenderer(int width, int height, int fontSize) {
    _width = width;
    _height = height;
    _fontSize = fontSize;
    calculateDimensions();
}

bool TextRenderer::loadFont(const uint8_t* data, size_t size) {
    if (_ofr.loadFont(data, size)) {
        _fontLoaded = true;
        _ofr.setCacheSize(10, 10, 32768); // Optimize with small cache (faces, sizes, bytes)
        calculateDimensions();
        return true;
    }
    return false;
}

void TextRenderer::calculateDimensions() {
    if (_fontLoaded) {
        _ofr.setFontSize(_fontSize * 10); // Scale font size for 7.5"
        // Approximate dimensions for wrap calculation
        _charsPerLine = (_width - 60) / (_fontSize * 8);
        _linesPerPage = (_height - 100) / (_fontSize * 12);
    } else {
        // Approximate character dimensions based on font size
        // GxEPD2 default font: size 1 = 6x8 pixels per char
        int charWidth = 6 * _fontSize;
        int lineHeight = 12 * _fontSize;  // 1.5x char height for better readability

        // Add some padding for the 7.5" screen
        int usableWidth = _width - 40;  // 20px margin on each side
        int usableHeight = _height - 80; // 40px top, 40px bottom

        _charsPerLine = usableWidth / charWidth;
        _linesPerPage = usableHeight / lineHeight;
    }
    
    Serial.printf("TextRenderer: %dx%d, fontSize=%d, chars/line=%d, lines/page=%d\n", 
                  _width, _height, _fontSize, _charsPerLine, _linesPerPage);
}

std::vector<String> TextRenderer::wrapText(const String& text) {
    std::vector<String> lines;
    lines.reserve(text.length() / _charsPerLine + 10); // Pre-allocate

    int textLen = text.length();
    int pos = 0;
    int yieldCounter = 0;

    while (pos < textLen) {
        // Yield periodically to prevent watchdog
        if(++yieldCounter % 50 == 0) yield();

        int remaining = textLen - pos;
        int chunkSize = (remaining < _charsPerLine) ? remaining : _charsPerLine;

        // Look for newline within chunk
        int newlinePos = text.indexOf('\n', pos);
        if(newlinePos >= 0 && newlinePos < pos + chunkSize) {
            chunkSize = newlinePos - pos;
        }
        // If we're not at the end or newline, try to break at a space
        else if (remaining > _charsPerLine) {
            // Search backwards from end of chunk for a space
            for(int i = pos + chunkSize - 1; i > pos; i--) {
                if(text.charAt(i) == ' ') {
                    chunkSize = i - pos + 1;
                    break;
                }
            }
        }

        if(chunkSize > 0) {
            String line = text.substring(pos, pos + chunkSize);
            line.trim();
            if (line.length() > 0) {
                lines.push_back(line);
            }
        }
        pos += chunkSize;
        if(chunkSize == 0) pos++; // Prevent infinite loop
    }

    return lines;
}

std::vector<String> TextRenderer::paginate(const String& text) {
    Serial.printf("TextRenderer::paginate start, text len=%d, free heap=%d\n",
                  text.length(), ESP.getFreeHeap());
    std::vector<String> pages;

    // Check memory before processing
    if(ESP.getFreeHeap() < 32768) {
        Serial.println("Not enough memory for pagination");
        pages.push_back("Memory low - cannot display chapter");
        return pages;
    }

    // First, wrap the text into lines
    std::vector<String> lines = wrapText(text);
    Serial.printf("Wrapped into %d lines\n", lines.size());

    // Estimate page count and reserve
    int estimatedPages = (lines.size() / _linesPerPage) + 1;
    pages.reserve(estimatedPages);

    // Then group lines into pages
    String currentPage;
    currentPage.reserve(_charsPerLine * _linesPerPage + _linesPerPage);
    int linesInPage = 0;

    for (size_t i = 0; i < lines.size(); i++) {
        if (linesInPage >= _linesPerPage) {
            pages.push_back(currentPage);
            currentPage = "";
            linesInPage = 0;
        }

        currentPage += lines[i];
        currentPage += '\n';
        linesInPage++;

        if(i % 100 == 0) yield();
    }

    if (currentPage.length() > 0) {
        pages.push_back(currentPage);
    }

    Serial.printf("Paginated into %d pages, free heap=%d\n", pages.size(), ESP.getFreeHeap());
    return pages;
}

void TextRenderer::renderPage(Book32Display& display, const String& pageText, int pageNum, int totalPages) {
    // Render text starting at top-left with margin
    int x = 20;
    int y = 40;
    
    if (_fontLoaded) {
        _ofr.setDrawer(display);
        _ofr.setFontColor(GxEPD_BLACK);
        _ofr.setFontSize(_fontSize * 12); // Slightly larger for better readability
        
        int lineStart = 0;
        int textLen = pageText.length();
        while (lineStart < textLen) {
            int lineEnd = pageText.indexOf('\n', lineStart);
            if (lineEnd == -1) lineEnd = textLen;

            String line = pageText.substring(lineStart, lineEnd);
            _ofr.setCursor(x, y);
            _ofr.printf("%s", line.c_str());

            y += (_fontSize * 14); // More line spacing
            lineStart = lineEnd + 1;
        }
    } else {
        display.setTextColor(GxEPD_BLACK);
        display.setTextSize(_fontSize);
        int lineHeight = 12 * _fontSize;

        int lineStart = 0;
        int textLen = pageText.length();
        while (lineStart < textLen) {
            int lineEnd = pageText.indexOf('\n', lineStart);
            if (lineEnd == -1) lineEnd = textLen;

            String line = pageText.substring(lineStart, lineEnd);
            display.setCursor(x, y);
            display.print(line);

            y += lineHeight;
            lineStart = lineEnd + 1;
        }
    }

    // Footer: page number
    display.setTextSize(2);
    display.setCursor(x, _height - 30);
    display.printf("%d / %d", pageNum + 1, totalPages);
}

// Rich content helper: Get text width in pixels
int TextRenderer::getTextWidth(Book32Display& display, const String& text, int fontSize) {
    if (_fontLoaded) {
        _ofr.setFontSize(fontSize * 8);
        return _ofr.getTextWidth(text.c_str());
    }
    return text.length() * 6 * fontSize;
}

// Rich content helper: Get font height in pixels
int TextRenderer::getFontHeight(int fontSize) {
    return 8 * fontSize;  // Base height is 8 pixels
}

// Render a single text node with formatting
void TextRenderer::renderTextNode(Book32Display& display, RichTextNode& node, int& y, int maxY) {
    if(y >= maxY) return;  // No more room on page
    
    int fontSize = _fontSize;
    
    // Determine font size based on style
    switch(node.style) {
        case STYLE_HEADER1: fontSize = _fontSize + 4; break;
        case STYLE_HEADER2: fontSize = _fontSize + 3; break;
        case STYLE_HEADER3: fontSize = _fontSize + 2; break;
        case STYLE_HEADER4: fontSize = _fontSize + 1; break;
        default: fontSize = _fontSize; break;
    }
    
    int lineHeight = getFontHeight(fontSize) + 6;
    int x_margin = 20;
    int usableWidth = _width - (x_margin * 2);
    
    if (_fontLoaded) {
        _ofr.setDrawer(display);
        _ofr.setFontSize(fontSize * 8);
        _ofr.setFontColor(GxEPD_BLACK);
        
        String text = node.text;
        if(node.isListItem) text = "• " + text;
        
        int pos = 0;
        while(pos < text.length() && y < maxY) {
            String line = "";
            int line_width = 0;
            int start_pos = pos;
            
            while(pos < text.length()) {
                int next_space = text.indexOf(' ', pos);
                if (next_space == -1) next_space = text.length();
                
                String word = text.substring(pos, next_space);
                if (pos != start_pos) word = " " + word;
                
                int word_width = _ofr.getTextWidth(word.c_str());
                if (line_width + word_width > usableWidth && line.length() > 0) break;
                
                line += word;
                line_width += word_width;
                pos = next_space;
                if (pos < text.length() && text.charAt(pos) == ' ') pos++;
            }
            
            int x = x_margin;
            if (node.align == ALIGN_CENTER) x = (_width - line_width) / 2;
            else if (node.align == ALIGN_RIGHT) x = _width - line_width - x_margin;
            
            _ofr.setCursor(x, y);
            _ofr.printf("%s", line.c_str());
            y += lineHeight;
        }
    } else {
        display.setTextSize(fontSize);
        String text = node.text;
        if(node.isListItem) text = "• " + text;
        
        int charWidth = 6 * fontSize;
        int charsPerLine = usableWidth / charWidth;
        
        int pos = 0;
        while(pos < text.length() && y < maxY) {
            int remaining = text.length() - pos;
            int chunkSize = (remaining < charsPerLine) ? remaining : charsPerLine;
            
            if(remaining > charsPerLine) {
                for(int i = pos + chunkSize - 1; i > pos; i--) {
                    if(text.charAt(i) == ' ') { chunkSize = i - pos + 1; break; }
                }
            }
            
            String line = text.substring(pos, pos + chunkSize);
            line.trim();
            
            if(line.length() > 0) {
                int line_width = line.length() * charWidth;
                int x = x_margin;
                if (node.align == ALIGN_CENTER) x = (_width - line_width) / 2;
                else if (node.align == ALIGN_RIGHT) x = _width - line_width - x_margin;
                
                display.setCursor(x, y);
                display.print(line);
                y += lineHeight;
            }
            pos += chunkSize;
            if(chunkSize == 0) pos++;
        }
    }
}

void TextRenderer::renderTable(Book32Display& display, Table& table, int& y, int maxY) {
    if(y >= maxY || table.rows.size() == 0) return;
    
    int tableWidth = _width - 40;
    int columnWidth = tableWidth / table.columnCount;
    int rowHeight = 30;
    
    for(size_t r = 0; r < table.rows.size() && y < maxY; r++) {
        TableRow& row = table.rows[r];
        int x = 20;
        for(size_t c = 0; c < row.cells.size() && c < (size_t)table.columnCount; c++) {
            TableCell& cell = row.cells[c];
            int cellWidth = columnWidth * cell.colspan;
            display.drawRect(x, y, cellWidth, rowHeight, GxEPD_BLACK);
            display.setTextSize(1);
            display.setCursor(x + 5, y + 10);
            String content = cell.content;
            if (content.length() > (size_t)(cellWidth/6)) content = content.substring(0, cellWidth/6 - 2) + "..";
            display.print(content);
            x += cellWidth;
        }
        y += rowHeight;
    }
    y += 10;
}

std::vector<String> TextRenderer::paginateRich(std::vector<ContentNode>& content) {
    Serial.printf("TextRenderer::paginateRich start, nodes=%d\n", content.size());
    std::vector<String> pages;
    String currentPage = "";
    int currentY = 0;
    int maxY = _height - 100; // Leave room for footer
    
    for(auto& node : content) {
        if(node.type == CONTENT_TEXT) {
            // Serialize node: T:style:align:isListItem:text
            String serialized = "T:" + String((int)node.textNode.style) + ":" + 
                                String((int)node.textNode.align) + ":" +
                                String(node.textNode.isListItem ? "1" : "0") + ":" +
                                node.textNode.text + "\n";
            
            // Heuristic for page overflow (simplified)
            int fontSize = _fontSize;
            switch(node.textNode.style) {
                case STYLE_HEADER1: fontSize += 4; break;
                case STYLE_HEADER2: fontSize += 3; break;
                default: break;
            }
            int lineHeight = getFontHeight(fontSize) + 6;
            int textLines = (node.textNode.text.length() / _charsPerLine) + 1;
            
            if(currentY + (textLines * lineHeight) > maxY && currentPage.length() > 0) {
                pages.push_back(currentPage);
                currentPage = "";
                currentY = 0;
            }
            
            currentPage += serialized;
            currentY += (textLines * lineHeight);
        }
        // TODO: Handle tables in serialization
        yield();
    }
    
    if(currentPage.length() > 0) pages.push_back(currentPage);
    
    Serial.printf("Paginated rich content into %d pages\n", pages.size());
    return pages;
}

void TextRenderer::renderRichPage(Book32Display& display, const String& pageData, int pageNum, int totalPages) {
    int y = 40;
    int maxY = _height - 60;
    int lineStart = 0;
    while(lineStart < pageData.length() && y < maxY) {
        int lineEnd = pageData.indexOf('\n', lineStart);
        if(lineEnd == -1) lineEnd = pageData.length();
        String line = pageData.substring(lineStart, lineEnd);
        if(line.startsWith("T:")) {
            int colon1 = line.indexOf(':', 2);
            int colon2 = line.indexOf(':', colon1 + 1);
            int colon3 = line.indexOf(':', colon2 + 1);
            if(colon1 != -1 && colon2 != -1 && colon3 != -1) {
                RichTextNode node;
                node.style = (TextStyle)line.substring(2, colon1).toInt();
                node.align = (TextAlign)line.substring(colon1 + 1, colon2).toInt();
                node.isListItem = line.substring(colon2 + 1, colon3) == "1";
                node.text = line.substring(colon3 + 1);
                renderTextNode(display, node, y, maxY);
            }
        }
        lineStart = lineEnd + 1;
    }
    display.setTextSize(2);
    display.setCursor(20, _height - 30);
    display.printf("%d / %d", pageNum + 1, totalPages);
}
