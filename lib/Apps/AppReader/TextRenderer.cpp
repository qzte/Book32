#include "TextRenderer.h"

TextRenderer::TextRenderer(int width, int height, int fontSize) {
    _width = width;
    _height = height;
    _fontSize = fontSize;
    calculateDimensions();
}

void TextRenderer::calculateDimensions() {
    // Approximate character dimensions based on font size
    // GxEPD2 default font: size 1 = 6x8 pixels per char
    int charWidth = 6 * _fontSize;
    int lineHeight = 12 * _fontSize;  // 1.5x char height for better readability

    // Add some padding
    int usableWidth = _width - 20;  // 10px margin on each side
    int usableHeight = _height - 40; // 20px top, 20px bottom

    _charsPerLine = usableWidth / charWidth;
    _linesPerPage = usableHeight / lineHeight;
    
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

    // Pre-calculate approximate page size
    int approxPageSize = _charsPerLine * _linesPerPage + _linesPerPage;

    // Then group lines into pages
    String currentPage;
    currentPage.reserve(approxPageSize);
    int linesInPage = 0;

    for (size_t i = 0; i < lines.size(); i++) {
        if (linesInPage >= _linesPerPage) {
            // Page is full, save it and start a new one
            pages.push_back(currentPage);
            currentPage = "";
            currentPage.reserve(approxPageSize);
            linesInPage = 0;
        }

        currentPage += lines[i];
        currentPage += '\n';
        linesInPage++;

        // Yield periodically
        if(i % 100 == 0) yield();
    }

    // Add the last page if it has content
    if (currentPage.length() > 0) {
        pages.push_back(currentPage);
    }

    // Clear lines vector to free memory
    lines.clear();
    lines.shrink_to_fit();

    Serial.printf("Paginated into %d pages, free heap=%d\n", pages.size(), ESP.getFreeHeap());
    return pages;
}

void TextRenderer::renderPage(Book32Display& display, const String& pageText, int pageNum, int totalPages) {
    // Drawing commands only - Loop and Window handled by Caller (AppReader)
    
    // Background is filled by caller usually, but strict drawing
    display.setTextColor(GxEPD_BLACK);
    display.setTextSize(_fontSize);

    // Render text starting at top-left with margin
    int x = 10;
    int y = 20;
    int lineHeight = 12 * _fontSize;  // 1.5x char height for readability

    // Split page text into lines and render each
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

    // Footer: page number
    display.setTextSize(1);
    display.setCursor(10, _height - 10);
    display.print(pageNum + 1);
    display.print(" / ");
    display.print(totalPages);
}

// Rich content helper: Get text width in pixels
int TextRenderer::getTextWidth(Book32Display& display, const String& text, int fontSize) {
    // Approximate: 6 pixels per character * fontSize
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
        case STYLE_HEADER1: fontSize = 4; break;
        case STYLE_HEADER2: fontSize = 3; break;
        case STYLE_HEADER3: fontSize = 2; break;
        case STYLE_HEADER4: fontSize = 2; break;
        case STYLE_BOLD: fontSize = _fontSize; break;
        case STYLE_ITALIC: fontSize = _fontSize; break;
        case STYLE_BOLD_ITALIC: fontSize = _fontSize; break;
        default: fontSize = _fontSize; break;
    }
    
    display.setTextSize(fontSize);
    int lineHeight = getFontHeight(fontSize) + 4;  // Add spacing
    
    // Handle list items
    String text = node.text;
    if(node.isListItem) {
        text = "• " + text;
    }
    
    // Word wrap the text
    int charWidth = 6 * fontSize;
    int usableWidth = _width - 20;  // Margins
    int charsPerLine = usableWidth / charWidth;
    
    // Split into lines
    int pos = 0;
    while(pos < text.length() && y < maxY) {
        int remaining = text.length() - pos;
        int chunkSize = (remaining < charsPerLine) ? remaining : charsPerLine;
        
        // Try to break at space
        if(remaining > charsPerLine) {
            for(int i = pos + chunkSize - 1; i > pos; i--) {
                if(text.charAt(i) == ' ') {
                    chunkSize = i - pos + 1;
                    break;
                }
            }
        }
        
        String line = text.substring(pos, pos + chunkSize);
        line.trim();
        
        if(line.length() > 0) {
            int x = 10;  // Default left margin
            
            // Handle text alignment
            int textWidth = getTextWidth(display, line, fontSize);
            switch(node.align) {
                case ALIGN_CENTER:
                    x = (_width - textWidth) / 2;
                    break;
                case ALIGN_RIGHT:
                    x = _width - textWidth - 10;
                    break;
                case ALIGN_JUSTIFY:
                    // TODO: Implement justify (add spacing between words)
                    x = 10;
                    break;
                default:  // ALIGN_LEFT
                    x = 10;
                    break;
            }
            
            display.setCursor(x, y);
            
            // Render with style
            if(node.style == STYLE_BOLD || node.style == STYLE_BOLD_ITALIC) {
                // Simulate bold by printing twice with 1px offset
                display.print(line);
                display.setCursor(x + 1, y);
                display.print(line);
            } else {
                display.print(line);
            }
            
            // TODO: Italic rendering (would need custom font or skewing)
            
            y += lineHeight;
        }
        
        pos += chunkSize;
        if(chunkSize == 0) pos++;  // Prevent infinite loop
    }
    
    // Add extra spacing after headers
    if(node.style >= STYLE_HEADER1 && node.style <= STYLE_HEADER4) {
        y += lineHeight / 2;
    }
}

// Render a table
void TextRenderer::renderTable(Book32Display& display, Table& table, int& y, int maxY) {
    if(y >= maxY || table.rows.size() == 0) return;
    
    int tableWidth = _width - 20;  // Margins
    int columnWidth = tableWidth / table.columnCount;
    int rowHeight = 20;  // Fixed row height
    
    int startY = y;
    
    // Draw table border
    display.drawRect(10, y, tableWidth, table.rows.size() * rowHeight, GxEPD_BLACK);
    
    // Render each row
    for(size_t r = 0; r < table.rows.size() && y < maxY; r++) {
        TableRow& row = table.rows[r];
        
        int x = 10;
        
        // Render each cell
        for(size_t c = 0; c < row.cells.size() && c < (size_t)table.columnCount; c++) {
            TableCell& cell = row.cells[c];
            
            int cellWidth = columnWidth * cell.colspan;
            int cellHeight = rowHeight * cell.rowspan;
            
            // Draw cell border
            display.drawRect(x, y, cellWidth, cellHeight, GxEPD_BLACK);
            
            // Render cell content
            display.setTextSize(1);
            if(cell.isHeader) {
                // Bold for headers (double print)
                display.setCursor(x + 2, y + 5);
                display.print(cell.content);
                display.setCursor(x + 3, y + 5);
                display.print(cell.content);
            } else {
                display.setCursor(x + 2, y + 5);
                display.print(cell.content);
            }
            
            x += cellWidth;
        }
        
        y += rowHeight;
    }
    
    y += 10;  // Spacing after table
}

// Paginate rich content
std::vector<String> TextRenderer::paginateRich(std::vector<ContentNode>& content) {
    Serial.printf("TextRenderer::paginateRich start, nodes=%d\n", content.size());
    std::vector<String> pages;
    
    // For now, we'll serialize the content nodes as a simple format
    // Format: "T:style:align:isListItem:text" for text, "TABLE:..." for tables
    // This is a simplified approach - in production you'd want a more robust format
    
    String currentPage;
    int estimatedLines = 0;
    
    for(auto& node : content) {
        if(node.type == CONTENT_TEXT) {
            // Estimate lines for this text node
            int fontSize = _fontSize;
            switch(node.textNode.style) {
                case STYLE_HEADER1: fontSize = 4; break;
                case STYLE_HEADER2: fontSize = 3; break;
                default: fontSize = _fontSize; break;
            }
            
            int charWidth = 6 * fontSize;
            int charsPerLine = (_width - 20) / charWidth;
            int textLines = (node.textNode.text.length() / charsPerLine) + 1;
            
            // Check if we need a new page
            if(estimatedLines + textLines > _linesPerPage && currentPage.length() > 0) {
                pages.push_back(currentPage);
                currentPage = "";
                estimatedLines = 0;
            }
            
            // Add node to current page
            currentPage += "T:" + String((int)node.textNode.style) + ":" + 
                          String((int)node.textNode.align) + ":" +
                          String(node.textNode.isListItem ? "1" : "0") + ":" +
                          node.textNode.text + "\n";
            estimatedLines += textLines;
            
        } else if(node.type == CONTENT_TABLE) {
            // Tables take significant space
            int tableLines = node.table.rows.size() + 2;
            
            if(estimatedLines + tableLines > _linesPerPage && currentPage.length() > 0) {
                pages.push_back(currentPage);
                currentPage = "";
                estimatedLines = 0;
            }
            
            // Serialize table (simplified)
            currentPage += "TABLE:" + String(node.table.columnCount) + ":" + 
                          String(node.table.rows.size()) + "\n";
            for(auto& row : node.table.rows) {
                for(auto& cell : row.cells) {
                    currentPage += cell.content + "|";
                }
                currentPage += "\n";
            }
            estimatedLines += tableLines;
        }
    }
    
    // Add last page
    if(currentPage.length() > 0) {
        pages.push_back(currentPage);
    }
    
    Serial.printf("Paginated rich content into %d pages\n", pages.size());
    return pages;
}

// Render a rich content page
void TextRenderer::renderRichPage(Book32Display& display, const String& pageData, int pageNum, int totalPages) {
    display.setTextColor(GxEPD_BLACK);
    
    int y = 20;
    int maxY = _height - 30;
    
    // Parse and render the page data
    int lineStart = 0;
    while(lineStart < pageData.length() && y < maxY) {
        int lineEnd = pageData.indexOf('\n', lineStart);
        if(lineEnd == -1) lineEnd = pageData.length();
        
        String line = pageData.substring(lineStart, lineEnd);
        
        if(line.startsWith("T:")) {
            // Text node: T:style:align:isListItem:text
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
        } else if(line.startsWith("TABLE:")) {
            // Table: TABLE:cols:rows
            // Followed by row data
            // (Simplified rendering - just show as text for now)
            display.setTextSize(1);
            display.setCursor(10, y);
            display.print("[Table]");
            y += 20;
        }
        
        lineStart = lineEnd + 1;
    }
    
    // Footer: page number
    display.setTextSize(1);
    display.setCursor(10, _height - 10);
    display.print(pageNum + 1);
    display.print(" / ");
    display.print(totalPages);
}
