#include "TextRenderer.h"

TextRenderer::TextRenderer(int width, int height, int fontSize) {
    _width = width;
    _height = height;
    _fontSize = fontSize;
    _fontLoaded = false;
    calculateDimensions();
}

bool TextRenderer::loadFont(const uint8_t* data, size_t size) {
    if (_ofr.loadFont(data, size)) {
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
        int usableWidth = _width - 60;
        int avgCharWidth = _ofr.getTextWidth("n");
        _charsPerLine = usableWidth / (avgCharWidth > 0 ? avgCharWidth : (_fontSize/2));
        int lineHeight = _fontSize + 8;
        _linesPerPage = (_height - 100) / lineHeight;
    } else {
        int charWidth = 6 * 2; // BACK TO 2 for 7.5" to avoid gigantic text
        _charsPerLine = (_width - 60) / charWidth;
        _linesPerPage = (_height - 100) / 20;
    }
    Serial.printf("TextRenderer Config: %dx%d, font=%d, cpl=%d, lpp=%d\n", _width, _height, _fontSize, _charsPerLine, _linesPerPage);
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
    int x = 30;
    int y = 50;
    int lineHeight = _fontSize + 10;
    if (_fontLoaded) {
        _ofr.setDrawer(display); _ofr.setFontColor(GxEPD_BLACK); _ofr.setFontSize(_fontSize);
        int lineStart = 0;
        while (lineStart < pageText.length()) {
            int lineEnd = pageText.indexOf('\n', lineStart);
            if (lineEnd == -1) lineEnd = pageText.length();
            _ofr.setCursor(x, y);
            _ofr.printf("%s", pageText.substring(lineStart, lineEnd).c_str());
            y += lineHeight; lineStart = lineEnd + 1;
        }
    } else {
        display.setTextColor(GxEPD_BLACK); display.setTextSize(2);
        int lineStart = 0;
        while (lineStart < pageText.length()) {
            int lineEnd = pageText.indexOf('\n', lineStart);
            if (lineEnd == -1) lineEnd = pageText.length();
            display.setCursor(x, y);
            display.print(pageText.substring(lineStart, lineEnd));
            y += 20; lineStart = lineEnd + 1;
        }
    }
    display.setTextSize(2);
    String footer = "Page " + String(pageNum + 1) + " of " + String(totalPages);
    display.setCursor((_width - footer.length()*12)/2, _height - 30);
    display.print(footer);
}

void TextRenderer::renderTextNode(Book32Display& display, RichTextNode& node, int& y, int maxY) {
    if (y >= maxY) return;
    int fontSize = _fontSize;
    if (node.style >= STYLE_HEADER1 && node.style <= STYLE_HEADER4) fontSize = _fontSize + (5 - (int)node.style) * 4;
    int x_margin = 30;
    int usableWidth = _width - (x_margin * 2);
    if (_fontLoaded) {
        _ofr.setDrawer(display); _ofr.setFontSize(fontSize); _ofr.setFontColor(GxEPD_BLACK);
        int currentX = x_margin;
        if (node.style == STYLE_NORMAL && !node.isListItem && node.text.length() > 20) currentX += 30; 
        String text = node.text; if (node.isListItem) text = "• " + text;
        int pos = 0;
        while(pos < text.length() && y < maxY) {
            String line = ""; int line_width = 0, start_pos = pos;
            while(pos < text.length()) {
                int next_space = text.indexOf(' ', pos);
                if (next_space == -1) next_space = text.length();
                String word = text.substring(pos, next_space);
                if (pos != start_pos) word = " " + word;
                int word_width = _ofr.getTextWidth(word.c_str());
                if (line_width + word_width > (usableWidth - (currentX - x_margin)) && line.length() > 0) break;
                line += word; line_width += word_width;
                pos = next_space; if (pos < text.length() && text.charAt(pos) == ' ') pos++;
            }
            int drawX = currentX;
            if (node.align == ALIGN_CENTER) drawX = (_width - line_width) / 2;
            else if (node.align == ALIGN_RIGHT) drawX = _width - line_width - x_margin;
            _ofr.setCursor(drawX, y); _ofr.printf("%s", line.c_str());
            y += fontSize + 8; currentX = x_margin;
        }
    } else {
        display.setTextSize(2);
        String text = node.text; if (node.isListItem) text = "• " + text;
        int charWidth = 6 * 2; int charsPerLine = usableWidth / charWidth;
        int pos = 0;
        while(pos < (int)text.length() && y < maxY) {
            int len = charsPerLine;
            if (pos + len < (int)text.length()) {
                int lastSpace = text.lastIndexOf(' ', pos + len);
                if (lastSpace > pos) len = lastSpace - pos + 1;
            }
            String line = text.substring(pos, pos + len);
            int drawX = x_margin;
            if (node.style == STYLE_NORMAL && !node.isListItem && pos == 0) drawX += 30;
            display.setCursor(drawX, y); display.print(line);
            y += 20; pos += len;
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
    int currentY = 50;
    int maxY = _height - 100;
    for (auto& node : content) {
        if (node.type == CONTENT_TEXT) {
            String serialized = "T:" + String((int)node.textNode.style) + ":" + String((int)node.textNode.align) + ":" + String(node.textNode.isListItem ? "1" : "0") + ":" + node.textNode.text + "\n";
            int fontSize = _fontLoaded ? _fontSize : 12;
            if (node.textNode.style >= STYLE_HEADER1) fontSize += 8;
            int textLines = (node.textNode.text.length() / _charsPerLine) + 1;
            int estHeight = textLines * (fontSize + 10);
            if (currentY + estHeight > maxY && currentPage.length() > 0) { pages.push_back(currentPage); currentPage = ""; currentY = 50; }
            currentPage += serialized;
            currentY += estHeight;
        }
        yield();
    }
    if (currentPage.length() > 0) pages.push_back(currentPage);
    return pages;
}

void TextRenderer::renderRichPage(Book32Display& display, const String& pageData, int pageNum, int totalPages) {
    int y = 50;
    int maxY = _height - 80;
    int lineStart = 0;
    while(lineStart < (int)pageData.length() && y < maxY) {
        int lineEnd = pageData.indexOf('\n', lineStart);
        if (lineEnd == -1) lineEnd = pageData.length();
        String line = pageData.substring(lineStart, lineEnd);
        if (line.startsWith("T:")) {
            int c1 = line.indexOf(':', 2), c2 = line.indexOf(':', c1 + 1), c3 = line.indexOf(':', c2 + 1);
            if (c1 != -1 && c2 != -1 && c3 != -1) {
                RichTextNode node;
                node.style = (TextStyle)line.substring(2, c1).toInt();
                node.align = (TextAlign)line.substring(c1 + 1, c2).toInt();
                node.isListItem = line.substring(c2 + 1, c3) == "1";
                node.text = line.substring(c3 + 1);
                renderTextNode(display, node, y, maxY);
            }
        }
        lineStart = lineEnd + 1;
    }
    display.setTextSize(2);
    String f = "Page " + String(pageNum + 1) + " of " + String(totalPages);
    display.setCursor((_width - f.length()*12)/2, _height - 30);
    display.print(f);
}
