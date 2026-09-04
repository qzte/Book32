#include "TextRenderer.h"
#include "WordFitLogic.h"
#include "HyphenationLogic.h"
#include "LineBreaker.h"
#include "CoverImage.h"
#include "ImageDither.h"

TextRenderer::TextRenderer(int width, int height, int fontSize) {
    _width = width;
    _height = height;
    // Normalize to a supported body size (9/12/18); default to small.
    if (fontSize >= 18) _fontSize = 18;
    else if (fontSize >= 12) _fontSize = 12;
    else _fontSize = 9;
    _cachedPage = -1;
    _lastGFXFont = nullptr;
    memset(_gfxCharWidths, 0, sizeof(_gfxCharWidths));
    calculateDimensions();
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
    int normalized =
        (family >= READER_FONT_SANS && family <= READER_FONT_OPEN_SANS) ? family : READER_FONT_SANS;
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
        case READER_FONT_OPEN_SANS:
            B32_FONT_SET(OpenSans_Regular9pt8b, OpenSans_Regular12pt8b, OpenSans_Regular18pt8b,
                         OpenSans_Bold9pt8b, OpenSans_Bold12pt8b, OpenSans_Bold18pt8b, OpenSans_Bold24pt8b)
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

RenderResult TextRenderer::renderRichPageDynamic(Book32Display& display,
                                                 const std::vector<ContentNode>& content, int startNode,
                                                 int startOffset, int pageNum, int pageNumForDisplay,
                                                 bool draw, EpubLoader* loader) {
    if (draw) {
        display.setTextColor(GxEPD_BLACK);
    }

    if (draw && _cachedPage == pageNum && !_lineCache.empty() && _hasCachedResult && !_cachedHasImage) {
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

    // line_width and currentX carry across node boundaries on purpose: an
    // inline run split across ContentNodes (e.g. plain text followed by a
    // bold span) keeps flowing on the same visual line instead of starting
    // a fresh one at each node.
    const int x_margin = 35;
    int currentX = x_margin;
    int line_width = 0;
    // D4: true right after a line ends with a syllable-hyphenated break, so
    // the very next line-wrap decision is forced to a plain wrap instead of
    // hyphenating again — two hyphenated line ends in a row reads worse than
    // an occasional wider gap. Cleared again on that next decision either way.
    bool justHyphenated = false;
    // Set once an image actually lands on this page; feeds _cachedHasImage
    // below so a later same-page cache-hit redraw doesn't skip it.
    bool pageHasImage = false;

    while (currentNode < (int)content.size() && y < maxY) {
        if (content[currentNode].type == CONTENT_TEXT) {
            RenderResult nodeFull = {0, 0, false, 0, 0};
            if (renderTextNode(display, content, currentNode, currentOffset, y, maxY, currentX, line_width,
                               justHyphenated, draw, nodeFull)) {
                result.pageFull = true;
                result.charsConsumedInLastNode = nodeFull.charsConsumedInLastNode;
                result.nextNodeIndex = nodeFull.nextNodeIndex;
                result.nextCharOffset = nodeFull.nextCharOffset;
                _cachedResult = result;
                _hasCachedResult = true;
                _cachedHasImage = pageHasImage;
                return result;
            }
        } else if (content[currentNode].type == CONTENT_IMAGE) {
            if (renderImageNode(display, content[currentNode], y, maxY, currentX, line_width, draw, loader)) {
                // Doesn't fit in what's left of this page: the whole node
                // (never partially drawn, unlike a text node) moves to the
                // next page.
                result.pageFull = true;
                result.charsConsumedInLastNode = 0;
                result.nextNodeIndex = currentNode;
                result.nextCharOffset = 0;
                _cachedResult = result;
                _hasCachedResult = true;
                _cachedHasImage = pageHasImage;
                return result;
            }
            pageHasImage = true;
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
    // If currentNode >= content.size(), all content was displayed -> pageFull stays false (true end of
    // chapter)

    // Page number drawing moved to AppReader for consistency
    _cachedResult = result;
    _hasCachedResult = true;
    _cachedHasImage = pageHasImage;
    return result;
}

// Word-wraps (and, when draw, draws) a single CONTENT_TEXT node starting at
// currentOffset. y/currentX/line_width/justHyphenated are the running
// layout state shared across nodes on the page (see the comment on
// line_width/currentX in renderRichPageDynamic) and are updated in place.
//
// Returns true if the page filled up partway through this node — `full` is
// then populated with the fields the caller should return immediately with
// (pageFull/charsConsumedInLastNode/nextNodeIndex/nextCharOffset). Returns
// false once the whole node has been consumed normally.
bool TextRenderer::renderTextNode(Book32Display& display, const std::vector<ContentNode>& content,
                                  int currentNode, int currentOffset, int& y, int maxY, int& currentX,
                                  int& line_width, bool& justHyphenated, bool draw, RenderResult& full) {
    const ContentNode& node = content[currentNode];
    const int x_margin = 35;

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

    // D2: drawX used to be currentX+line_width with a hardcoded
    // override for STYLE_HEADER1/2 at each of the three commit sites
    // below (and the middle one didn't even have that override,
    // so a header line that got wrapped mid-content drew its first
    // line unaligned). Centering/right-alignment now follow the
    // node's own .align — set from CSS text-align on <p>/<div> as
    // well as headers (EpubLoader.cpp) — instead of only ever
    // reacting to the header style.
    auto computeDrawX = [&](int segWidth) -> int {
        if (node.textNode.align == ALIGN_CENTER) return (_width - segWidth) / 2;
        if (node.textNode.align == ALIGN_RIGHT) return (_width - x_margin) - segWidth;
        return currentX + line_width;
    };

    if (node.textNode.isBlockStart && currentOffset == 0) {
        if (line_width > 0) {
            y += nodeLineHeight;
            line_width = 0;
        }
        // A <br> soft break never gets the first-line indent a real
        // block does (D3).
        currentX = node.textNode.softBreak ? x_margin : x_margin + node.textNode.indent;

        // Add extra spacing before headers
        if (node.textNode.style == STYLE_HEADER1) {
            y += 30;      // Big gap before chapter title
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
    char lineBuf[256];

    while (pos < textLen && y < maxY) {
        int line_chars = 0;
        lineBuf[0] = '\0';
        int segment_width = 0;

        if (node.textNode.isListItem && pos == currentOffset) {
            // Ordered (<ol>) items carry their 1-based ordinal in
            // listNumber; unordered (<ul>) items and lists that
            // never resolved to a stack depth (listNumber == 0)
            // keep the plain dash marker.
            if (node.textNode.listNumber > 0) {
                snprintf(lineBuf, sizeof(lineBuf), "%d. ", node.textNode.listNumber);
            } else {
                strcpy(lineBuf, "- ");
            }
            segment_width = 0;
            for (const char* m = lineBuf; *m; m++) {
                segment_width += _gfxCharWidths[(unsigned char)*m];
            }
        }

        while (pos + line_chars < textLen) {
            int wordStart = pos + line_chars;
            while (wordStart < textLen && isspace((unsigned char)text[wordStart]))
                wordStart++;
            if (wordStart >= textLen) {
                line_chars = textLen - pos;
                break;
            }

            int wordEnd = wordStart;
            while (wordEnd < textLen && !isspace((unsigned char)text[wordEnd]))
                wordEnd++;

            int wordWidth = 0;
            for (int k = wordStart; k < wordEnd; k++) {
                unsigned char c = (unsigned char)text[k];
                wordWidth += _gfxCharWidths[c];
            }

            int spaceWidth = (line_width + segment_width > 0) ? _gfxCharWidths[' '] : 0;
            int usableWidth = _width - x_margin;

            if (currentX + line_width + segment_width + spaceWidth + wordWidth > usableWidth &&
                (line_width + segment_width) > 0) {
                // Word doesn't fit on this line
                if (y + nodeLineHeight > maxY) {
                    if (strlen(lineBuf) > 0) {
                        int drawX = computeDrawX(segment_width);
                        _lineCache.push_back({drawX, y, (int)node.textNode.style, false, String(lineBuf)});
                        if (draw) {
                            display.setCursor(drawX, y);
                            display.print(lineBuf);
                        }
                    }

                    int nextOffset = pos + line_chars;
                    full.pageFull = true;
                    full.charsConsumedInLastNode = nextOffset;
                    full.nextNodeIndex = currentNode;
                    full.nextCharOffset = nextOffset;
                    return true;
                }

                // D4: before giving up on this line, try a syllable break
                // that fits what's still left on it (see tryHyphenateAtWrap
                // in LineBreaker.h — pure, host-tested in
                // tools/tests/test_line_breaker.cpp).
                int hyphBudget = usableWidth - (currentX + line_width + segment_width + spaceWidth);
                int hyphBufLeft = (int)sizeof(lineBuf) - 1 - (int)strlen(lineBuf) - (spaceWidth > 0 ? 1 : 0);
                WordFit hfit = tryHyphenateAtWrap(text + wordStart, wordEnd - wordStart, wordWidth,
                                                  hyphBufLeft, hyphBudget, _gfxCharWidths, justHyphenated);
                if (hfit.hyphen && hfit.take > 0) {
                    if (spaceWidth > 0) {
                        strcat(lineBuf, " ");
                        segment_width += spaceWidth;
                    }
                    strncat(lineBuf, text + wordStart, hfit.take);
                    strcat(lineBuf, "-");
                    segment_width += hfit.width;

                    int drawX = computeDrawX(segment_width);
                    _lineCache.push_back({drawX, y, (int)node.textNode.style, false, String(lineBuf)});
                    if (draw) {
                        display.setCursor(drawX, y);
                        display.print(lineBuf);
                    }

                    line_chars = wordStart + hfit.take - pos;
                    y += nodeLineHeight;
                    line_width = 0;
                    currentX = x_margin;
                    segment_width = 0;
                    lineBuf[0] = '\0';
                    justHyphenated = true;
                    continue;
                }
                justHyphenated = false;

                // Commit current segment before starting new line
                if (segment_width > 0) {
                    int drawX = computeDrawX(segment_width);
                    _lineCache.push_back({drawX, y, (int)node.textNode.style, false, String(lineBuf)});
                    if (draw) {
                        display.setCursor(drawX, y);
                        display.print(lineBuf);
                    }
                }

                y += nodeLineHeight;
                line_width = 0;
                currentX = x_margin;
                segment_width = 0;
                lineBuf[0] = '\0';

                // Retest the word on the new line
                spaceWidth = 0;
            }

            // Space left in lineBuf, recomputed before every write: the
            // appends below are clamped to it. Without the clamp a
            // single token longer than the buffer (a URL, or text the
            // HTML parser failed to split) ran past the end of this
            // stack buffer — the wrap branch above only fires once the
            // line already holds something, so the first word of a
            // line was always copied whole.
            int bufLeft = (int)sizeof(lineBuf) - 1 - (int)strlen(lineBuf);

            if (segment_width > 0 || line_width > 0) {
                if (bufLeft <= 0) break; // no room even for a separator
                strcat(lineBuf, " ");
                segment_width += spaceWidth;
                bufLeft--;
            }

            // A word still too wide here cannot fit a line of its own
            // either. Prefer breaking at a Portuguese syllable
            // boundary with a visible hyphen (see HyphenationLogic.h)
            // over the plain by-character split (WordFitLogic.h, host
            // test tools/tests/test_word_fit.cpp). hyphenationPoints()
            // allocates, so it's only computed for the word-doesn't-
            // fit case handled here — the common whole-word-fits case
            // below never reaches it.
            int wordLen = wordEnd - wordStart;
            int pixelBudget = usableWidth - (currentX + line_width + segment_width);
            WordFit fit;
            if (wordLen <= bufLeft && wordWidth <= pixelBudget) {
                fit = {wordLen, wordWidth, false};
            } else {
                std::vector<int> hpoints = hyphenationPoints(text + wordStart, wordLen);
                fit = fitWordIntoLineHyphenated(text + wordStart, wordLen, wordWidth, bufLeft, pixelBudget,
                                                _gfxCharWidths, hpoints);
            }
            if (fit.take <= 0) break; // buffer full: commit this line

            strncat(lineBuf, text + wordStart, fit.take);
            if (fit.hyphen) strcat(lineBuf, "-");
            segment_width += fit.width;
            line_chars = wordStart + fit.take - pos;
            if (fit.take < wordLen) break; // remainder goes on the next line
        }

        if (strlen(lineBuf) > 0) {
            int drawX = computeDrawX(segment_width);
            _lineCache.push_back({drawX, y, (int)node.textNode.style, false, String(lineBuf)});
            if (draw) {
                display.setCursor(drawX, y);
                display.print(lineBuf);
            }
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
        full.pageFull = true;
        full.charsConsumedInLastNode = pos;
        full.nextNodeIndex = currentNode;
        full.nextCharOffset = pos;
        return true;
    }

    if (node.textNode.isBlockStart && currentNode < (int)content.size() - 1 &&
        content[currentNode + 1].textNode.isBlockStart && !content[currentNode + 1].textNode.softBreak) {
        y += 8; // Paragraph gap (not for a <br> soft break, see D3)
    }
    // Add extra spacing after headers
    if (node.textNode.style == STYLE_HEADER1) {
        y += 25; // Extra gap after chapter title
    } else if (node.textNode.style == STYLE_HEADER2) {
        y += 15;
    } else if (node.textNode.style == STYLE_HEADER3) {
        y += 10;
    }

    return false;
}

// Lays out (and, when draw, draws) one CONTENT_IMAGE node — see the
// TextRenderer.h header comment for the full contract.
bool TextRenderer::renderImageNode(Book32Display& display, const ContentNode& node, int& y, int maxY,
                                   int& currentX, int& line_width, bool draw, EpubLoader* loader) {
    const int x_margin = 35;
    const ImageNode& img = node.imageNode;

    // Probe the natural size once (cached on the node itself, see the
    // `mutable` fields in EpubLoader.h) so repeated passes over the same
    // chapter — measurement scans, a same-page redraw — don't reopen the ZIP
    // just to find out how tall the image is.
    if (!img.probed) {
        img.probed = true;
        if (loader && img.zipPath.length() > 0) {
            size_t size = 0;
            uint8_t* data = loader->getFontData(img.zipPath, &size);
            if (data) {
                if (size > 0) probeImageDimensions(data, size, &img.naturalWidth, &img.naturalHeight);
                free(data);
            }
        }
    }

    // An image always starts its own line, like a block element: flush
    // whatever text line was in progress first.
    if (line_width > 0) {
        y += _lineHeight;
        line_width = 0;
    }
    currentX = x_margin;

    if (img.naturalWidth <= 0 || img.naturalHeight <= 0) {
        // Couldn't read the file — missing from the archive, a format
        // decodeCoverToBitmap doesn't recognize, or corrupt. Same fail-open
        // contract as the cover: the book keeps reading, just without this
        // one illustration, instead of stalling or dropping the chapter.
        return false;
    }

    const int boxW = _width - 2 * x_margin;
    const int boxTop = 40;
    const int boxHFull = maxY - boxTop; // an empty page always has room for it
    book32::FitRect fit = book32::fitInsideBox(img.naturalWidth, img.naturalHeight, boxW, boxHFull);
    if (fit.w <= 0 || fit.h <= 0) return false;

    if (y + fit.h > maxY && y > boxTop) {
        // Doesn't fit in what's left of this page, and the page already has
        // something on it: move the whole image to the next page instead of
        // clipping it (unlike a text node, an image is never drawn partway).
        return true;
    }

    if (draw) {
        size_t size = 0;
        uint8_t* data = loader ? loader->getFontData(img.zipPath, &size) : nullptr;
        if (data) {
            if (size > 0) {
                const size_t bitmapBytes = (size_t)((fit.w + 7) / 8) * (size_t)fit.h;
                uint8_t* bitmap = (uint8_t*)ps_malloc(bitmapBytes);
                if (!bitmap) bitmap = (uint8_t*)malloc(bitmapBytes);
                if (bitmap) {
                    if (decodeCoverToBitmap(data, size, fit.w, fit.h, bitmap, nullptr)) {
                        int drawX = x_margin + (boxW - fit.w) / 2;
                        display.drawBitmap(drawX, y, bitmap, fit.w, fit.h, GxEPD_BLACK);
                    }
                    free(bitmap);
                }
            }
            free(data);
        }
    }

    y += fit.h + 12; // breathing room before whatever follows
    return false;
}
