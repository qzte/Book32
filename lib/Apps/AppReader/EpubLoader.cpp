#include "EpubLoader.h"
#include "Book32FS.h"
#include <LittleFS.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <algorithm>

#ifndef ZIP_SUCCESS
#define ZIP_SUCCESS 0
#endif

static int zipFd = -1;

void *myOpen(const char *filename, int32_t *size) {
    if (zipFd >= 0) { close(zipFd); zipFd = -1; }
    String fullPath = filename;
    if (!fullPath.startsWith("/littlefs") && !fullPath.startsWith("/ebooks")) fullPath = "/littlefs" + fullPath;
    zipFd = open(fullPath.c_str(), O_RDONLY);
    if (zipFd < 0) return NULL;
    struct stat st;
    if (fstat(zipFd, &st) != 0) { close(zipFd); zipFd = -1; return NULL; }
    *size = st.st_size;
    return (void*)(intptr_t)(zipFd + 1);
}

void myClose(void *p) { if (zipFd >= 0) { close(zipFd); zipFd = -1; } }
int32_t myRead(void *p, uint8_t *buffer, int32_t length) {
    if (zipFd < 0 || !buffer || length <= 0) return -1;
    return (int32_t)read(zipFd, buffer, length);
}
int32_t mySeek(void *p, int32_t position, int iType) {
    if (zipFd < 0) return -1;
    return (int32_t)lseek(zipFd, position, iType);
}

EpubLoader::EpubLoader() {
    zip = (UNZIP*)ps_malloc(sizeof(UNZIP));
    if (!zip) zip = new (std::nothrow) UNZIP();
    else new (zip) UNZIP();
}

EpubLoader::~EpubLoader() { if (zip) { zip->~UNZIP(); free(zip); zip = nullptr; } }

bool EpubLoader::open(const char* path) {
    epubPath = String(path);
    if (zip->openZIP(path, myOpen, myClose, myRead, mySeek) != ZIP_SUCCESS) return false;
    if (!parseContainer()) { close(); return false; }
    if (!parseOpf()) { close(); return false; }
    return true;
}

void EpubLoader::close() {
    if (zip) zip->closeZIP();
    spine.clear(); manifest.clear(); fonts.clear();
}

String EpubLoader::getTitle() { return bookTitle; }
int EpubLoader::getChapterCount() { return spine.size(); }

String EpubLoader::getChapterContent(int index) {
    if(index < 0 || index >= (int)spine.size()) return "";
    String href = spine[index].href;
    String fullPath = rootDir + href;
    if(fullPath.startsWith("./")) fullPath = fullPath.substring(2);
    String content = readFileFromZip(fullPath.c_str());
    if (content.length() == 0) return "";
    
    // --- ADVANCED HTML PARSING ---
    String clean;
    clean.reserve(content.length());
    bool inTag = false, skipContent = false;
    String currentTag;
    
    for(int i = 0; i < (int)content.length(); i++) {
        char c = content.charAt(i);
        if(c == '<') {
            inTag = true; currentTag = ""; int j = i + 1;
            while(j < (int)content.length() && content.charAt(j) != '>' && content.charAt(j) != ' ' && j - i < 20) { currentTag += (char)tolower(content.charAt(j)); j++; }
            
            // BLOCK ELEMENTS: cause a newline
            if(currentTag == "p" || currentTag == "/p" || currentTag == "div" || currentTag == "/div" || currentTag == "br" || currentTag == "br/" || currentTag.startsWith("h")) {
                if(clean.length() > 0 && clean.charAt(clean.length()-1) != '\n') clean += "\n";
            }
            else if(currentTag == "li") {
                if(clean.length() > 0 && clean.charAt(clean.length()-1) != '\n') clean += "\n";
                clean += "• ";
            }
            // Skip image/media elements completely
            else if(currentTag == "img" || currentTag == "svg" || currentTag == "figure" || currentTag == "image") {
                // Skip - these are self-closing or we don't want their content
            }
            else if(currentTag == "/figure" || currentTag == "/svg") {
                // End of skipped elements
            }
            else if(currentTag == "script" || currentTag == "style" || currentTag == "head") skipContent = true;
            else if(currentTag == "/script" || currentTag == "/style" || currentTag == "/head") skipContent = false;
        } else if (c == '>') {
            inTag = false;
        } else if (!inTag && !skipContent) {
            if(c == '\n' || c == '\r' || c == '\t') c = ' ';
            clean += c;
        }
    }

    // --- AGGRESSIVE CLEANING ---
    // Handle Windows-1252 / UTF-8 mix-up artifacts seen in Sanderson EPUBs
    clean.replace("¶Ç8", " -- ");
    clean.replace("¶ÇÖ", "'");
    clean.replace("¶Çö", "'");
    clean.replace("¶Ç£", "\"");
    clean.replace("¶Ç¥", "\"");
    clean.replace("¶Çª", "-");
    clean.replace("¶ÇÜ", "...");
    clean.replace("¶Ç", ""); // Wipe any remaining prefix
    
    // Standard UTF-8
    clean.replace("\xE2\x80\x9C", "\""); clean.replace("\xE2\x80\x9D", "\"");
    clean.replace("\xE2\x80\x98", "'"); clean.replace("\xE2\x80\x99", "'");
    clean.replace("\xE2\x80\x94", " -- "); clean.replace("\xE2\x80\x93", " - ");
    clean.replace("\xE2\x80\xA6", "...");
    
    // Strip accidental newlines before punctuation (fixes the orphan comma/dot)
    clean.replace("\n,", ",");
    clean.replace("\n.", ".");
    clean.replace("\n?", "?");
    clean.replace("\n!", "!");
    clean.replace("\n\"", "\"");
    clean.replace("\n'", "'");
    
    // Collapse multiple spaces
    while(clean.indexOf("  ") != -1) clean.replace("  ", " ");
    
    clean.trim();
    return clean;
}

bool EpubLoader::parseContainer() {
    String xml = readFileFromZip("META-INF/container.xml");
    if(xml.length() == 0) return false;
    opfPath = extractAttribute(xml, "rootfile", "full-path");
    if(opfPath.length() == 0) return false;
    int lastSlash = opfPath.lastIndexOf('/');
    if(lastSlash != -1) rootDir = opfPath.substring(0, lastSlash + 1);
    else rootDir = "";
    return true;
}

bool EpubLoader::parseOpf() {
    String xml = readFileFromZip(opfPath.c_str());
    if(xml.length() == 0) return false;
    bookTitle = extractMetadata(xml, "dc:title");
    if(bookTitle.length() == 0) bookTitle = extractMetadata(xml, "title");
    int manifestStart = xml.indexOf("<manifest");
    int manifestEnd = xml.indexOf("</manifest>");
    if(manifestStart == -1 || manifestEnd == -1) return false;
    String manifestBlock = xml.substring(manifestStart, manifestEnd);
    int pos = 0;
    while(true) {
        int itemStart = manifestBlock.indexOf("<item", pos);
        if(itemStart == -1) break;
        int itemEnd = manifestBlock.indexOf(">", itemStart);
        String itemTag = manifestBlock.substring(itemStart, itemEnd+1);
        String id = extractAttribute(itemTag, "item", "id");
        String href = extractAttribute(itemTag, "item", "href");
        String mediaType = extractAttribute(itemTag, "item", "media-type");
        if(id.length() > 0 && href.length() > 0) {
            manifest[id] = href;
            String hrefLower = href; hrefLower.toLowerCase();
            if(hrefLower.endsWith(".ttf") || hrefLower.endsWith(".otf") || mediaType.indexOf("font") != -1) {
                FontInfo font; font.path = rootDir + href;
                if(hrefLower.endsWith(".ttf")) font.format = "ttf";
                else if(hrefLower.endsWith(".otf")) font.format = "otf";
                int lastSlash = href.lastIndexOf('/'), lastDot = href.lastIndexOf('.');
                if(lastSlash != -1 && lastDot != -1) font.family = href.substring(lastSlash + 1, lastDot);
                else if(lastDot != -1) font.family = href.substring(0, lastDot);
                String fLower = font.family; fLower.toLowerCase();
                if(fLower.indexOf("bolditalic") != -1) font.style = "bold-italic";
                else if(fLower.indexOf("bold") != -1) font.style = "bold";
                else if(fLower.indexOf("italic") != -1) font.style = "italic";
                else font.style = "normal";
                fonts.push_back(font);
            }
            if(itemTag.indexOf("properties=\"cover-image\"") != -1) coverPath = rootDir + href;
        }
        pos = itemEnd;
    }
    int spineStart = xml.indexOf("<spine"), spineEnd = xml.indexOf("</spine>");
    if(spineStart == -1 || spineEnd == -1) return false;
    String spineBlock = xml.substring(spineStart, spineEnd);
    pos = 0;
    while(true) {
        int itemRefStart = spineBlock.indexOf("<itemref", pos);
        if(itemRefStart == -1) break;
        int itemRefEnd = spineBlock.indexOf(">", itemRefStart);
        String itemRefTag = spineBlock.substring(itemRefStart, itemRefEnd+1);
        String idref = extractAttribute(itemRefTag, "itemref", "idref");
        if(idref.length() > 0 && manifest.count(idref)) {
            SpineItem item; item.id = idref; item.href = manifest[idref];
            spine.push_back(item);
        }
        pos = itemRefEnd;
    }
    return true;
}

uint8_t* EpubLoader::getFontData(String path, size_t* outSize) {
    if(path.length() == 0) return nullptr;
    if (zip->locateFile(path.c_str()) != 0) return nullptr;
    if (zip->openCurrentFile() != 0) return nullptr;
    unz_file_info fileInfo;
    char szName[256];
    zip->getFileInfo(&fileInfo, szName, sizeof(szName), NULL, 0, NULL, 0);
    size_t size = fileInfo.uncompressed_size;
    uint8_t* buffer = (uint8_t*)ps_malloc(size);
    if(!buffer) buffer = (uint8_t*)malloc(size);
    if(!buffer) { zip->closeCurrentFile(); return nullptr; }
    zip->readCurrentFile(buffer, size);
    zip->closeCurrentFile();
    *outSize = size;
    return buffer;
}

String EpubLoader::extractAttribute(String xml, String tag, String attr) {
    int attrStart = xml.indexOf(attr + "=\"");
    if(attrStart == -1) attrStart = xml.indexOf(attr + "='");
    if(attrStart == -1) return "";
    int valStart = attrStart + attr.length() + 2; 
    char quote = xml.charAt(attrStart + attr.length() + 1); 
    int valEnd = xml.indexOf(quote, valStart);
    if(valEnd == -1) return "";
    return xml.substring(valStart, valEnd);
}

String EpubLoader::extractMetadata(String xml, String tag) {
    int tagStart = xml.indexOf("<" + tag);
    if(tagStart == -1) return "";
    int tagEnd = xml.indexOf(">", tagStart);
    if(tagEnd == -1) return "";
    int contentEnd = xml.indexOf("</" + tag + ">", tagEnd);
    if(contentEnd == -1) contentEnd = xml.indexOf("</", tagEnd);
    if(contentEnd == -1) return "";
    String content = xml.substring(tagEnd + 1, contentEnd);
    content.trim();
    return content;
}

String EpubLoader::readFileFromZip(const char* path) {
    if (zip->locateFile(path) != ZIP_SUCCESS) return "";
    if (zip->openCurrentFile() != ZIP_SUCCESS) return "";
    unz_file_info fileInfo;
    char szName[256];
    zip->getFileInfo(&fileInfo, szName, sizeof(szName), NULL, 0, NULL, 0);
    int size = fileInfo.uncompressed_size;
    uint8_t *buffer = (uint8_t*)ps_malloc(size + 1);
    if(!buffer) buffer = (uint8_t*)malloc(size + 1);
    if(!buffer) { zip->closeCurrentFile(); return ""; }
    zip->readCurrentFile(buffer, size);
    buffer[size] = 0;
    String str = String((char*)buffer);
    free(buffer);
    zip->closeCurrentFile();
    return str;
}

String EpubLoader::getAuthor() { return bookAuthor; }
String EpubLoader::getPublisher() { return bookPublisher; }
String EpubLoader::getLanguage() { return bookLanguage; }
String EpubLoader::getPublicationDate() { return bookPubDate; }
String EpubLoader::getISBN() { return bookISBN; }
std::vector<FontInfo> EpubLoader::getFonts() { return fonts; }
String EpubLoader::getCoverPath() { return coverPath; }

uint8_t* EpubLoader::getCoverData(size_t* outSize) {
    if(coverPath.length() == 0) return nullptr;
    if (zip->locateFile(coverPath.c_str()) != 0) return nullptr;
    if (zip->openCurrentFile() != 0) return nullptr;
    unz_file_info fileInfo;
    char szName[256];
    zip->getFileInfo(&fileInfo, szName, sizeof(szName), NULL, 0, NULL, 0);
    size_t size = fileInfo.uncompressed_size;
    uint8_t* buffer = (uint8_t*)ps_malloc(size);
    if(!buffer) buffer = (uint8_t*)malloc(size);
    if(!buffer) { zip->closeCurrentFile(); return nullptr; }
    zip->readCurrentFile(buffer, size);
    zip->closeCurrentFile();
    *outSize = size;
    return buffer;
}

TextStyle EpubLoader::getStyleFromTag(String tag) {
    tag.toLowerCase();
    if(tag == "b" || tag == "strong") return STYLE_BOLD;
    if(tag == "i" || tag == "em") return STYLE_ITALIC;
    if(tag == "h1") return STYLE_HEADER1;
    if(tag == "h2") return STYLE_HEADER2;
    if(tag == "h3") return STYLE_HEADER3;
    if(tag == "h4") return STYLE_HEADER4;
    return STYLE_NORMAL;
}

TextAlign EpubLoader::getAlignFromStyle(String styleAttr) {
    styleAttr.toLowerCase();
    if(styleAttr.indexOf("text-align:center") != -1 || styleAttr.indexOf("text-align: center") != -1) return ALIGN_CENTER;
    if(styleAttr.indexOf("text-align:right") != -1 || styleAttr.indexOf("text-align: right") != -1) return ALIGN_RIGHT;
    if(styleAttr.indexOf("text-align:justify") != -1 || styleAttr.indexOf("text-align: justify") != -1) return ALIGN_JUSTIFY;
    return ALIGN_LEFT;
}

Table EpubLoader::parseTable(String tableHtml) {
    Table table;
    int trPos = 0;
    while(true) {
        int trStart = tableHtml.indexOf("<tr", trPos);
        if(trStart == -1) break;
        int trEnd = tableHtml.indexOf("</tr>", trStart);
        if(trEnd == -1) break;
        String rowHtml = tableHtml.substring(trStart, trEnd + 5);
        TableRow row;
        int cellPos = 0;
        while(true) {
            int tdStart = rowHtml.indexOf("<td", cellPos);
            int thStart = rowHtml.indexOf("<th", cellPos);
            int cellStart = -1;
            bool isHeader = false;
            if(tdStart != -1 && (thStart == -1 || tdStart < thStart)) { cellStart = tdStart; isHeader = false; }
            else if(thStart != -1) { cellStart = thStart; isHeader = true; }
            if(cellStart == -1) break;
            String cellTag = isHeader ? "th" : "td";
            int cellTagEnd = rowHtml.indexOf(">", cellStart);
            int cellEnd = rowHtml.indexOf("</" + cellTag + ">", cellTagEnd);
            if(cellTagEnd == -1 || cellEnd == -1) break;
            TableCell cell;
            cell.isHeader = isHeader;
            String cellOpenTag = rowHtml.substring(cellStart, cellTagEnd + 1);
            String colspanStr = extractAttribute(cellOpenTag, cellTag, "colspan");
            String rowspanStr = extractAttribute(cellOpenTag, cellTag, "rowspan");
            if(colspanStr.length() > 0) cell.colspan = colspanStr.toInt();
            if(rowspanStr.length() > 0) cell.rowspan = rowspanStr.toInt();
            String cellContent = rowHtml.substring(cellTagEnd + 1, cellEnd);
            String clean;
            bool inTag = false;
            for(int i = 0; i < (int)cellContent.length(); i++) {
                char c = cellContent.charAt(i);
                if(c == '<') inTag = true;
                else if(c == '>') inTag = false;
                else if(!inTag) clean += c;
            }
            clean.trim();
            cell.content = clean;
            row.cells.push_back(cell);
            cellPos = cellEnd + cellTag.length() + 3;
        }
        if(row.cells.size() > 0) {
            table.rows.push_back(row);
            if((int)row.cells.size() > table.columnCount) table.columnCount = row.cells.size();
        }
        trPos = trEnd + 5;
    }
    return table;
}

int extractIndentFromStyle(String styleAttr) {
    styleAttr.toLowerCase();
    int indentPos = styleAttr.indexOf("text-indent:");
    if (indentPos == -1) indentPos = styleAttr.indexOf("text-indent :");
    if (indentPos != -1) {
        int valStart = styleAttr.indexOf(':', indentPos) + 1;
        int valEnd = styleAttr.indexOf(';', valStart);
        if (valEnd == -1) valEnd = styleAttr.length();
        String val = styleAttr.substring(valStart, valEnd);
        val.trim();
        // Handle em, px, %
        if (val.endsWith("em")) return val.substring(0, val.length()-2).toInt() * 20; // Rough 1em = 20px
        if (val.endsWith("px")) return val.substring(0, val.length()-2).toInt();
        return val.toInt();
    }
    return 0;
}

std::vector<ContentNode> EpubLoader::parseHtmlToRichContent(String html) {
    std::vector<ContentNode> nodes;
    std::vector<TextStyle> styleStack;
    styleStack.push_back(STYLE_NORMAL);
    TextAlign currentAlign = ALIGN_LEFT;
    int currentIndent = 0;
    bool isListItem = false;
    bool nextIsBlockStart = true;
    String currentText;
    int i = 0;
    while(i < (int)html.length()) {
        char c = html.charAt(i);
        if(c == '<') {
            if(currentText.length() > 0) {
                ContentNode node;
                node.type = CONTENT_TEXT;
                node.textNode.text = currentText;
                node.textNode.style = styleStack.back();
                node.textNode.align = currentAlign;
                node.textNode.isListItem = isListItem;
                node.textNode.indent = currentIndent;
                node.textNode.isBlockStart = nextIsBlockStart;
                nodes.push_back(node);
                currentText = "";
                isListItem = false;
                currentIndent = 0;
                nextIsBlockStart = false; // Next node in same block is not a start
            }
            int tagEnd = html.indexOf('>', i);
            if(tagEnd == -1) break;
            String fullTag = html.substring(i, tagEnd + 1);
            String tag;
            int spacePos = fullTag.indexOf(' ');
            int closePos = fullTag.indexOf('>');
            if(spacePos != -1 && spacePos < closePos) tag = fullTag.substring(1, spacePos);
            else tag = fullTag.substring(1, closePos);
            tag.toLowerCase();
            bool isClosing = tag.startsWith("/");
            if(isClosing) tag = tag.substring(1);
            
            // Handle inline styling elements
            if(tag == "b" || tag == "strong" || tag == "i" || tag == "em") {
                if(!isClosing) styleStack.push_back(getStyleFromTag(tag));
                else if(styleStack.size() > 1) styleStack.pop_back();
            }
            // Handle header elements - they are BOTH styled AND block elements
            else if(tag == "h1" || tag == "h2" || tag == "h3" || tag == "h4" || tag == "h5" || tag == "h6") {
                if(!isClosing) {
                    styleStack.push_back(getStyleFromTag(tag));
                    nextIsBlockStart = true;
                } else {
                    if(styleStack.size() > 1) styleStack.pop_back();
                    nextIsBlockStart = true;
                }
            }
            else if((tag == "p" || tag == "div" || tag.startsWith("h")) && !isClosing) {
                nextIsBlockStart = true;
                String styleAttr = extractAttribute(fullTag, tag, "style");
                String classAttr = extractAttribute(fullTag, tag, "class");
                classAttr.toLowerCase();
                
                // Detect chapter numbers/titles by CSS class
                // Be specific to avoid false positives like "title-page" on copyright pages
                if(classAttr.indexOf("chapter") != -1 || classAttr.indexOf("chap-") != -1 ||
                   classAttr.indexOf("heading") != -1 || classAttr.indexOf("section-title") != -1 ||
                   (classAttr.indexOf("ct") != -1 && classAttr.indexOf("ct") == 0) ||
                   (classAttr.indexOf("cn") != -1 && classAttr.indexOf("cn") == 0)) {
                    // This is likely a chapter number/title - use header style
                    styleStack.push_back(STYLE_HEADER1);
                }
                
                if(styleAttr.length() > 0) {
                    currentAlign = getAlignFromStyle(styleAttr);
                    currentIndent = extractIndentFromStyle(styleAttr);
                }
                if (tag == "p" && currentIndent == 0 && styleStack.back() == STYLE_NORMAL) {
                    currentIndent = 30; 
                }
            }
            else if(tag == "/p" || tag == "/div" || tag.startsWith("/h")) {
                nextIsBlockStart = true;
                // Pop any header style that was pushed for this block
                if(styleStack.size() > 1 && (styleStack.back() == STYLE_HEADER1 || 
                   styleStack.back() == STYLE_HEADER2 || styleStack.back() == STYLE_HEADER3)) {
                    styleStack.pop_back();
                }
            }
            else if(tag == "li" && !isClosing) {
                isListItem = true;
                nextIsBlockStart = true;
            }
            else if(tag == "table" && !isClosing) {
                int tableEnd = html.indexOf("</table>", i);
                if(tableEnd != -1) {
                    String tableHtml = html.substring(i, tableEnd + 8);
                    Table table = parseTable(tableHtml);
                    if(table.rows.size() > 0) {
                        ContentNode node;
                        node.type = CONTENT_TABLE;
                        node.table = table;
                        nodes.push_back(node);
                    }
                    i = tableEnd + 8;
                    nextIsBlockStart = true;
                    continue;
                }
            }
            else if(tag == "script" || tag == "style" || tag == "head" || tag == "figure" || tag == "svg" || tag == "figcaption") {
                int skipEnd = html.indexOf("</" + tag + ">", i);
                if(skipEnd != -1) { i = skipEnd + tag.length() + 3; continue; }
            }
            // Skip self-closing image tags
            else if(tag == "img" || tag == "image") {
                // Just skip - nothing to do for self-closing tags
            }
            else if(tag == "br") {
                currentText += "\n";
            }
            i = tagEnd + 1;
        } else {
            if(c == '\n' || c == '\r' || c == '\t' || c == ' ') {
                if(currentText.length() > 0 && currentText.charAt(currentText.length()-1) != ' ' && currentText.charAt(currentText.length()-1) != '\n') {
                    currentText += ' ';
                }
            } else {
                currentText += c;
            }
            i++;
        }
    }
    if(currentText.length() > 0) {
        ContentNode node;
        node.type = CONTENT_TEXT;
        node.textNode.text = currentText;
        node.textNode.style = styleStack.back();
        node.textNode.align = currentAlign;
        node.textNode.isListItem = isListItem;
        node.textNode.indent = currentIndent;
        node.textNode.isBlockStart = nextIsBlockStart;
        nodes.push_back(node);
    }
    for(auto& node : nodes) {
        if(node.type == CONTENT_TEXT) {
            node.textNode.text.replace("¶Ç8", " -- ");
            node.textNode.text.replace("¶ÇÖ", "'");
            node.textNode.text.replace("¶Çö", "'");
            node.textNode.text.replace("¶Ç£", "\"");
            node.textNode.text.replace("¶Ç¥", "\"");
            node.textNode.text.replace("¶Ç", " ");
            node.textNode.text.replace("\xE2\x80\x9C", "\"");
            node.textNode.text.replace("\xE2\x80\x9D", "\"");
            node.textNode.text.replace("\xE2\x80\x98", "'");
            node.textNode.text.replace("\xE2\x80\x99", "'");
            node.textNode.text.replace("\xE2\x80\x94", " -- ");
            node.textNode.text.replace("\xE2\x80\x93", " - ");
            node.textNode.text.replace("\xE2\x80\xA6", "...");
            node.textNode.text.replace("\n,", ",");
            node.textNode.text.replace("\n.", ".");
            node.textNode.text.replace("\n!", "!");
            node.textNode.text.replace("\n?", "?");
            node.textNode.text.trim();
            // Filter out common image alt text placeholders
            if(node.textNode.text == "Unknown" || node.textNode.text == "image" || 
               node.textNode.text == "Image" || node.textNode.text == "[image]") {
                node.textNode.text = "";
            }
            
            // Heuristic: Short numeric content (1-3 digits) that starts a block is likely a chapter number
            if(node.textNode.isBlockStart && node.textNode.text.length() > 0 && node.textNode.text.length() <= 3) {
                bool isNumeric = true;
                for(int i = 0; i < (int)node.textNode.text.length(); i++) {
                    if(!isdigit(node.textNode.text.charAt(i))) { isNumeric = false; break; }
                }
                if(isNumeric) {
                    node.textNode.style = STYLE_HEADER1; // Chapter number - use big centered style
                }
            }
        }
    }
    // Remove empty text nodes
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [](const ContentNode& n) {
        return n.type == CONTENT_TEXT && n.textNode.text.length() == 0;
    }), nodes.end());
    return nodes;
}

std::vector<ContentNode> EpubLoader::getChapterContentRich(int index) {
    if(index < 0 || index >= (int)spine.size()) return std::vector<ContentNode>();
    String href = spine[index].href;
    String fullPath = rootDir + href;
    if(fullPath.startsWith("./")) fullPath = fullPath.substring(2);
    String content = readFileFromZip(fullPath.c_str());
    return parseHtmlToRichContent(content);
}
