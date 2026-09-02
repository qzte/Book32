#include "EpubLoader.h"
#include "Book32FS.h"
#include "FontMgr.h"
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
    if (zipFd >= 0) { ::close(zipFd); zipFd = -1; }
    spine.clear(); manifest.clear(); fonts.clear();
}

String EpubLoader::getTitle() { return bookTitle; }
int EpubLoader::getChapterCount() { return spine.size(); }

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

    // Capa (recurso EPUB2): <meta name="cover" content="ID_DO_MANIFEST"/>,
    // normalmente dentro de <metadata>, antes do <manifest>. O id só se pode
    // resolver depois de o manifest estar todo lido (linha abaixo), por isso
    // fica só guardado aqui.
    String coverMetaId;
    {
        int metaPos = xml.indexOf("name=\"cover\"");
        if (metaPos == -1) metaPos = xml.indexOf("name='cover'");
        if (metaPos != -1) {
            int tagStart = xml.lastIndexOf('<', metaPos);
            int tagEnd = xml.indexOf('>', metaPos);
            if (tagStart != -1 && tagEnd != -1 && tagEnd > tagStart) {
                String metaTag = xml.substring(tagStart, tagEnd + 1);
                coverMetaId = extractAttribute(metaTag, "meta", "content");
            }
        }
    }

    String manifestBlock = xml.substring(manifestStart, manifestEnd);
    int pos = 0;
    while(true) {
        int itemStart = manifestBlock.indexOf("<item", pos);
        if(itemStart == -1) break;
        int itemEnd = manifestBlock.indexOf(">", itemStart);
        // Um '<item' sem '>' (OPF truncado ou mal formado) devolvia -1 aqui:
        // substring(itemStart, 0) trocava os limites e, sobretudo, pos ficava
        // a -1, o que faz o indexOf seguinte ler fora do buffer e o ciclo
        // nunca terminar. O ficheiro vem de um EPUB do utilizador, por isso
        // tem de falhar em silêncio e não travar o leitor.
        if(itemEnd == -1) break;
        String itemTag = manifestBlock.substring(itemStart, itemEnd+1);
        String id = extractAttribute(itemTag, "item", "id");
        String href = extractAttribute(itemTag, "item", "href");
        String mediaType = extractAttribute(itemTag, "item", "media-type");
        if(id.length() > 0 && href.length() > 0) {
            manifest[id] = href;
            // Capa (EPUB3): properties="cover-image" no próprio <item>. Tem
            // prioridade sobre o <meta name="cover"> do EPUB2 abaixo — é a
            // forma actual do standard e não depende de resolver um id.
            String properties = extractAttribute(itemTag, "item", "properties");
            if (coverHref.length() == 0 && properties.indexOf("cover-image") != -1) {
                coverHref = href;
            }
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
        }
        pos = itemEnd + 1;
    }

    // Resolve o recurso EPUB2, agora que o manifest está todo lido, só se o
    // EPUB3 acima não encontrou nada.
    if (coverHref.length() == 0 && coverMetaId.length() > 0 && manifest.count(coverMetaId)) {
        coverHref = manifest[coverMetaId];
    }

    int spineStart = xml.indexOf("<spine"), spineEnd = xml.indexOf("</spine>");
    if(spineStart == -1 || spineEnd == -1) return false;
    String spineBlock = xml.substring(spineStart, spineEnd);
    pos = 0;
    while(true) {
        int itemRefStart = spineBlock.indexOf("<itemref", pos);
        if(itemRefStart == -1) break;
        int itemRefEnd = spineBlock.indexOf(">", itemRefStart);
        if(itemRefEnd == -1) break;  // mesma razão do ciclo do manifest
        String itemRefTag = spineBlock.substring(itemRefStart, itemRefEnd+1);
        String idref = extractAttribute(itemRefTag, "itemref", "idref");
        if(idref.length() > 0 && manifest.count(idref)) {
            SpineItem item; item.id = idref; item.href = manifest[idref];
            spine.push_back(item);
        }
        pos = itemRefEnd + 1;
    }

    parseGuide(xml);

    return true;
}

// Tipos de <reference type="..."> do <guide> (OPF2, secção 2.6) que não são
// conteúdo de leitura corrido. Conservador de propósito: fica de fora tudo o
// que costuma ter texto que o utilizador quer mesmo ler (prefácio, posfácio,
// epígrafe, o próprio "text") — só os tipos inequivocamente estruturais
// entram aqui, mesmos exemplos concretos apontados em
// docs/plans/2026-08-31-avaliacao-gestao-epub.md (página de rosto, página de
// créditos, a própria página de navegação).
static bool isNarrativeGuideType(const String& type) {
    static const char* NON_NARRATIVE[] = {
        "cover", "toc",      "title-page",   "copyright-page", "dedication",
        "index", "glossary", "bibliography", "colophon",       "acknowledgements",
        "loi",   "lot",      "notes",
    };
    for (const char* t : NON_NARRATIVE) {
        if (type.equalsIgnoreCase(t)) return false;
    }
    return true;
}

void EpubLoader::parseGuide(const String& xml) {
    int guideStart = xml.indexOf("<guide");
    if (guideStart == -1) return; // comum em EPUB3 puro — sem <guide>, tudo fica narrativo
    int guideEnd = xml.indexOf("</guide>", guideStart);
    if (guideEnd == -1) return;
    String guideBlock = xml.substring(guideStart, guideEnd);

    // href -> não-narrativo, resolvido depois de percorrer todo o <guide>
    // (mais do que uma <reference> pode apontar para o mesmo documento).
    // guideTypeForHref guarda o tipo cru (ex.: "cover") para o mesmo href, só
    // para os não-narrativos — é a informação que nonNarrativeHref já não
    // carrega, usada para rotular a entrada quando não há título detectável
    // (ver EpubLoader::getChapterGuideType / BookIndexer).
    std::map<String, bool> nonNarrativeHref;
    std::map<String, String> guideTypeForHref;

    int pos = 0;
    while (true) {
        int refStart = guideBlock.indexOf("<reference", pos);
        if (refStart == -1) break;
        int refEnd = guideBlock.indexOf(">", refStart);
        if (refEnd == -1) break; // OPF truncado — falha em silêncio, mesma regra do manifest/spine
        String refTag = guideBlock.substring(refStart, refEnd + 1);
        String type = extractAttribute(refTag, "reference", "type");
        String href = extractAttribute(refTag, "reference", "href");
        pos = refEnd + 1;
        if (href.length() == 0 || type.length() == 0) continue;
        int hashPos = href.indexOf('#');
        if (hashPos != -1)
            href = href.substring(0, hashPos); // referências ao próprio ficheiro, ignora o fragmento
        if (!isNarrativeGuideType(type)) {
            nonNarrativeHref[href] = true;
            guideTypeForHref[href] = type;
        }
    }
    if (nonNarrativeHref.empty()) return;

    nonNarrativeChapters.assign(spine.size(), false);
    chapterGuideType.assign(spine.size(), "");
    for (size_t i = 0; i < spine.size(); i++) {
        if (nonNarrativeHref.count(spine[i].href)) {
            nonNarrativeChapters[i] = true;
            chapterGuideType[i] = guideTypeForHref[spine[i].href];
        }
    }
}

bool EpubLoader::isChapterNarrative(int index) const {
    if (index < 0 || index >= (int)nonNarrativeChapters.size()) return true;
    return !nonNarrativeChapters[index];
}

String EpubLoader::getChapterGuideType(int index) const {
    if (index < 0 || index >= (int)chapterGuideType.size()) return "";
    return chapterGuideType[index];
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
    // D12: a short or failed read used to fall through silently, handing
    // JPEGDEC a buffer with uninitialized bytes past whatever did get read
    // as if it were the whole cover image.
    int bytesRead = zip->readCurrentFile(buffer, size);
    zip->closeCurrentFile();
    if (bytesRead != (int)size) {
        free(buffer);
        return nullptr;
    }
    *outSize = size;
    return buffer;
}

uint8_t* EpubLoader::getCoverImageData(size_t* outSize) {
    if (coverHref.length() == 0) return nullptr;
    String fullPath = rootDir + coverHref;
    if (fullPath.startsWith("./")) fullPath = fullPath.substring(2);
    return getFontData(fullPath, outSize); // mesmo mecanismo de leitura do ZIP, nome à parte
}

// D12: xml.indexOf(needle) alone matched needle anywhere in the tag, so a
// search for "type=\"" matched inside "media-type=\"...\"" and a search for
// "href=\"" matched inside "data-href=\"...\"". None of today's callers
// actually hit this (the tags where "type" is searched don't carry
// "media-type", etc.), but it's a latent trap for the next attribute added.
// A real attribute is always preceded by whitespace (or starts the string),
// never by another identifier character.
static int findAttributeStart(const String& xml, const String& needle, int fromIndex) {
    while (true) {
        int pos = xml.indexOf(needle, fromIndex);
        if (pos == -1) return -1;
        if (pos == 0 || isspace((unsigned char)xml.charAt(pos - 1))) return pos;
        fromIndex = pos + 1;
    }
}

String EpubLoader::extractAttribute(const String& xml, const String& tag, const String& attr) {
    int attrStart = findAttributeStart(xml, attr + "=\"", 0);
    if (attrStart == -1) attrStart = findAttributeStart(xml, attr + "='", 0);
    if(attrStart == -1) return "";
    int valStart = attrStart + attr.length() + 2; 
    char quote = xml.charAt(attrStart + attr.length() + 1); 
    int valEnd = xml.indexOf(quote, valStart);
    if(valEnd == -1) return "";
    return xml.substring(valStart, valEnd);
}

String EpubLoader::extractMetadata(const String& xml, const String& tag) {
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

// Tecto para o que se carrega de dentro do ZIP para uma String. O tamanho vem
// do cabeçalho do EPUB, ou seja, de um ficheiro do utilizador: sem tecto, um
// capítulo gigante (ou um cabeçalho a mentir) pedia esse tamanho à heap
// interna, que tem umas centenas de KB, e o leitor morria a abrir o livro.
// Truncar deixa o capítulo incompleto mas o dispositivo de pé.
static const int BOOK32_MAX_ZIP_TEXT_BYTES = 256 * 1024;

String EpubLoader::readFileFromZip(const char* path) {
    if (zip->locateFile(path) != ZIP_SUCCESS) return "";
    if (zip->openCurrentFile() != ZIP_SUCCESS) return "";
    unz_file_info fileInfo;
    char szName[256];
    zip->getFileInfo(&fileInfo, szName, sizeof(szName), NULL, 0, NULL, 0);
    int size = fileInfo.uncompressed_size;
    if (size > BOOK32_MAX_ZIP_TEXT_BYTES) {
        Serial.printf("EpubLoader: %s tem %d bytes; a truncar em %d\n",
                      path, size, BOOK32_MAX_ZIP_TEXT_BYTES);
        size = BOOK32_MAX_ZIP_TEXT_BYTES;
    }

    String str;
    str.reserve(size + 1);
    char buffer[513];
    int remaining = size;
    while (remaining > 0) {
        int toRead = remaining > 512 ? 512 : remaining;
        int bytesRead = zip->readCurrentFile((uint8_t*)buffer, toRead);
        if (bytesRead <= 0) break;
        // D12: str += buffer relied on the '\0' just written at
        // buffer[bytesRead] and stopped there — a literal null byte inside
        // the XHTML itself (rare, but seen in badly-converted UTF-16 files)
        // silently truncated the rest of the chapter. concat(buffer, len)
        // copies exactly bytesRead bytes regardless of what's in them.
        str.concat(buffer, bytesRead);
        remaining -= bytesRead;
        yield();
    }

    zip->closeCurrentFile();
    return str;
}

String EpubLoader::getAuthor() { return bookAuthor; }
String EpubLoader::getPublisher() { return bookPublisher; }
String EpubLoader::getLanguage() { return bookLanguage; }
String EpubLoader::getPublicationDate() { return bookPubDate; }
String EpubLoader::getISBN() { return bookISBN; }
std::vector<FontInfo> EpubLoader::getFonts() { return fonts; }

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

Table EpubLoader::parseTable(const String& tableHtml) {
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
        // D12: String::toInt() parses only the leading digits, so a
        // fractional value like "1.5em" (a real text-indent in EPUB CSS)
        // stopped at the '.' and came back as 1 instead of 1.5. atof()
        // reads the whole number; the (int) truncation below is the same
        // "round toward zero" toInt() already did for whole numbers.
        if (val.endsWith("em"))
            return (int)(atof(val.substring(0, val.length() - 2).c_str()) * 20); // Rough 1em = 20px
        if (val.endsWith("px")) return (int)atof(val.substring(0, val.length() - 2).c_str());
        return (int)atof(val.c_str());
    }
    return 0;
}


// Decode the HTML character entities that matter for Portuguese EPUB text.
// Named entities are mapped straight to Latin-1 bytes; numeric entities
// (&#231; / &#xE7;) are emitted as UTF-8 so the subsequent utf8ToLatin1()
// pass normalizes everything through a single code path.
static void appendCodepointUtf8(String& out, uint32_t cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += '?';
    }
}

static void decodeHtmlEntities(String& text) {
    if (text.indexOf('&') == -1) return;

    struct Entity { const char* name; const char* value; };
    // Latin-1 values are written as escaped bytes so this file stays ASCII.
    static const Entity entities[] = {
        {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"},
        {"nbsp", " "}, {"shy", ""},
        {"aacute", "\xE1"}, {"agrave", "\xE0"}, {"acirc", "\xE2"}, {"atilde", "\xE3"}, {"auml", "\xE4"},
        {"Aacute", "\xC1"}, {"Agrave", "\xC0"}, {"Acirc", "\xC2"}, {"Atilde", "\xC3"}, {"Auml", "\xC4"},
        {"ccedil", "\xE7"}, {"Ccedil", "\xC7"},
        {"eacute", "\xE9"}, {"egrave", "\xE8"}, {"ecirc", "\xEA"}, {"euml", "\xEB"},
        {"Eacute", "\xC9"}, {"Egrave", "\xC8"}, {"Ecirc", "\xCA"}, {"Euml", "\xCB"},
        {"iacute", "\xED"}, {"igrave", "\xEC"}, {"icirc", "\xEE"}, {"iuml", "\xEF"},
        {"Iacute", "\xCD"}, {"Igrave", "\xCC"}, {"Icirc", "\xCE"}, {"Iuml", "\xCF"},
        {"oacute", "\xF3"}, {"ograve", "\xF2"}, {"ocirc", "\xF4"}, {"otilde", "\xF5"}, {"ouml", "\xF6"},
        {"Oacute", "\xD3"}, {"Ograve", "\xD2"}, {"Ocirc", "\xD4"}, {"Otilde", "\xD5"}, {"Ouml", "\xD6"},
        {"uacute", "\xFA"}, {"ugrave", "\xF9"}, {"ucirc", "\xFB"}, {"uuml", "\xFC"},
        {"Uacute", "\xDA"}, {"Ugrave", "\xD9"}, {"Ucirc", "\xDB"}, {"Uuml", "\xDC"},
        {"ntilde", "\xF1"}, {"Ntilde", "\xD1"},
        {"laquo", "\xAB"}, {"raquo", "\xBB"},
        {"ordf", "\xAA"}, {"ordm", "\xBA"}, {"deg", "\xB0"},
        {"ndash", "-"}, {"mdash", "-"}, {"hellip", "..."},
        {"lsquo", "'"}, {"rsquo", "'"}, {"ldquo", "\""}, {"rdquo", "\""},
    };

    String out;
    out.reserve(text.length());
    int i = 0;
    int len = text.length();
    while (i < len) {
        char c = text.charAt(i);
        if (c != '&') { out += c; i++; continue; }
        int semi = text.indexOf(';', i + 1);
        // Entities are short; an unmatched or distant ';' means a literal '&'.
        if (semi == -1 || semi - i > 10) { out += c; i++; continue; }
        String body = text.substring(i + 1, semi);
        bool handled = false;
        if (body.length() > 1 && body.charAt(0) == '#') {
            uint32_t cp = 0;
            if (body.charAt(1) == 'x' || body.charAt(1) == 'X') {
                cp = (uint32_t)strtoul(body.c_str() + 2, nullptr, 16);
            } else {
                cp = (uint32_t)strtoul(body.c_str() + 1, nullptr, 10);
            }
            if (cp > 0) { appendCodepointUtf8(out, cp); handled = true; }
        } else {
            for (const Entity& e : entities) {
                if (body.equals(e.name)) { out += e.value; handled = true; break; }
            }
        }
        if (handled) { i = semi + 1; }
        else { out += c; i++; }
    }
    text = out;
}

std::vector<ContentNode> EpubLoader::parseHtmlToRichContent(const String& html) {
    std::vector<ContentNode> nodes;
    std::vector<TextStyle> styleStack;
    styleStack.push_back(STYLE_NORMAL);
    TextAlign currentAlign = ALIGN_LEFT;
    int currentIndent = 0;
    bool isListItem = false;
    int currentListNumber = 0; // Set from listStack when a <li> opens; 0 = no number (<ul> or no list)
    bool nextIsBlockStart = true;
    // true when the NEXT flushed node's isBlockStart is due to a <br> rather
    // than a real block element (see D3); consumed by the flush below.
    bool nextIsSoftBreak = false;
    // styleStack.size() saved at each block open (p/div/hN/li), so the
    // matching close can restore the stack to exactly that size regardless
    // of which inline b/i/em/strong tags were left unclosed inside it, and
    // reset currentAlign/currentIndent instead of leaking them into whatever
    // follows (D2, D10). Not a true tag-name-matched stack — a stray close
    // just pops the innermost marker — but sufficient for the indexOf-based
    // parser this is (see M3 for the real fix).
    std::vector<size_t> blockStack;
    String currentText;
    // D12: currentText grows one character at a time below (`currentText +=
    // c`), and String::concat() only ever grows the buffer to exactly what
    // the new length needs — no doubling — so every single-char append past
    // the current capacity reallocates. A 100+ KB chapter is tens of
    // thousands of one-byte reallocations without this. Reserving a
    // reasonable head start per text run (below, wherever currentText resets
    // to "") covers ordinary paragraphs with zero reallocations and still
    // only reallocates a handful of times for a long one.
    static const size_t TEXT_RUN_RESERVE = 256;
    currentText.reserve(TEXT_RUN_RESERVE);
    int i = 0;
    // One entry per open <ol>/<ul>, innermost last.
    struct OpenList {
        bool ordered;
        int nextNumber;
    };
    std::vector<OpenList> listStack;
    static const int LIST_INDENT_STEP =
        20; // px per nesting level, matches the renderer's flat left-margin model
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
                node.textNode.listNumber = currentListNumber;
                node.textNode.isBlockStart = nextIsBlockStart;
                node.textNode.softBreak = nextIsSoftBreak;
                nodes.push_back(node);
                currentText = "";
                currentText.reserve(TEXT_RUN_RESERVE);
                isListItem = false;
                currentIndent = 0;
                currentListNumber = 0;
                nextIsBlockStart = false; // Next node in same block is not a start
                nextIsSoftBreak = false;
            }

            // D9: the tag boundary below is found with indexOf('>', i), which
            // a comment or CDATA section defeats the moment it contains a
            // '>' of its own — a commented-out tag (<!-- <p>rascunho</p> -->,
            // common in EPUB produced by hand or by Sigil) or any markup-
            // looking draft text dumped everything after that first '>' as
            // visible text instead of being skipped. Handle both wholesale,
            // before the general tag parsing below ever sees them.
            if (html.indexOf("<!--", i) == i) {
                int commentEnd = html.indexOf("-->", i + 4);
                i = (commentEnd == -1) ? (int)html.length() : commentEnd + 3;
                continue;
            }
            if (html.indexOf("<![CDATA[", i) == i) {
                int cdataEnd = html.indexOf("]]>", i + 9);
                i = (cdataEnd == -1) ? (int)html.length() : cdataEnd + 3;
                continue;
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
                    blockStack.push_back(styleStack.size());
                    styleStack.push_back(getStyleFromTag(tag));
                    // Headers centre by default (H1/H2 only, matching the
                    // renderer's long-standing look); an explicit style="" on
                    // the tag itself isn't read here, same as before.
                    currentAlign = (tag == "h1" || tag == "h2") ? ALIGN_CENTER : ALIGN_LEFT;
                    nextIsBlockStart = true;
                } else {
                    if (!blockStack.empty()) {
                        size_t sz = blockStack.back();
                        blockStack.pop_back();
                        if (sz < 1) sz = 1;
                        if (sz < styleStack.size()) styleStack.resize(sz);
                    } else if (styleStack.size() > 1)
                        styleStack.pop_back();
                    currentAlign = ALIGN_LEFT;
                    currentIndent = 0;
                    nextIsBlockStart = true;
                }
            }
            else if((tag == "p" || tag == "div" || tag.startsWith("h")) && !isClosing) {
                blockStack.push_back(styleStack.size());
                nextIsBlockStart = true;
                String styleAttr = extractAttribute(fullTag, tag, "style");
                String classAttr = extractAttribute(fullTag, tag, "class");
                classAttr.toLowerCase();

                // Detect chapter numbers/titles by CSS class
                // Be very conservative - actual headers use <h1>-<h6> tags which are handled separately
                // Only match very specific chapter/title class patterns to avoid false positives
                if(classAttr.indexOf("chapter-title") != -1 || classAttr.indexOf("chap-title") != -1 ||
                   classAttr.indexOf("section-title") != -1 || classAttr.indexOf("part-title") != -1) {
                    // This is likely a chapter/section title - use header style
                    styleStack.push_back(STYLE_HEADER1);
                    currentAlign = ALIGN_CENTER;
                }

                if(styleAttr.length() > 0) {
                    currentAlign = getAlignFromStyle(styleAttr);
                    currentIndent = extractIndentFromStyle(styleAttr);
                }
                if (tag == "p" && currentIndent == 0 && styleStack.back() == STYLE_NORMAL) {
                    currentIndent = 30;
                }
            } else if (isClosing && (tag == "p" || tag == "div" || tag.startsWith("h"))) {
                // `tag` never actually starts with "/" here — the leading
                // slash is stripped above before any of these comparisons —
                // so this used to never match "/p"/"/div"/"/h..." and never
                // ran (D10): a </p>/</div> popped nothing off styleStack and
                // never reset currentAlign/currentIndent, so an unclosed
                // <b>/<i> or an explicit text-align leaked into everything
                // that followed. Restore the block-open marker instead of
                // guessing which header level to pop.
                if (!blockStack.empty()) {
                    size_t sz = blockStack.back();
                    blockStack.pop_back();
                    if (sz < 1) sz = 1;
                    if (sz < styleStack.size()) styleStack.resize(sz);
                }
                currentAlign = ALIGN_LEFT;
                currentIndent = 0;
                nextIsBlockStart = true;
            } else if ((tag == "ol" || tag == "ul") && !isClosing) {
                listStack.push_back({tag == "ol", 1});
            } else if (tag == "ol" || tag == "ul") { // closing
                if (!listStack.empty()) listStack.pop_back();
            } else if (tag == "li" && !isClosing) {
                blockStack.push_back(styleStack.size());
                isListItem = true;
                nextIsBlockStart = true;
                currentIndent = LIST_INDENT_STEP * (int)listStack.size();
                if (!listStack.empty() && listStack.back().ordered) {
                    currentListNumber = listStack.back().nextNumber++;
                } else {
                    currentListNumber = 0;
                }
            } else if (tag == "li" && isClosing) {
                if (!blockStack.empty()) {
                    size_t sz = blockStack.back();
                    blockStack.pop_back();
                    if (sz < 1) sz = 1;
                    if (sz < styleStack.size()) styleStack.resize(sz);
                }
                currentAlign = ALIGN_LEFT;
                currentIndent = 0;
                nextIsBlockStart = true;
            } else if (tag == "table" && !isClosing) {
                int tableEnd = html.indexOf("</table>", i);
                if(tableEnd != -1) {
                    String tableHtml = html.substring(i, tableEnd + 8);
                    Table table = parseTable(tableHtml);
                    // D1: parseTable() still does the structural work (colspan/
                    // rowspan/th aside, only the cell text matters here), but the
                    // renderer only ever draws CONTENT_TEXT — a CONTENT_TABLE node
                    // used to reach the screen as nothing at all. Turn each row
                    // into a plain paragraph instead: cells joined by " | " for a
                    // narrow table, one indented line per cell when there are more
                    // than two columns (a wide table wrapped to " | " would run off
                    // the 480px column anyway).
                    bool oneCellPerLine = table.columnCount > 2;
                    for (const TableRow& row : table.rows) {
                        if (row.cells.empty()) continue;
                        if (oneCellPerLine) {
                            for (const TableCell& cell : row.cells) {
                                if (cell.content.length() == 0) continue;
                                ContentNode node;
                                node.type = CONTENT_TEXT;
                                node.textNode.text = cell.content;
                                node.textNode.style = cell.isHeader ? STYLE_BOLD : STYLE_NORMAL;
                                node.textNode.isBlockStart = true;
                                node.textNode.indent = 20;
                                nodes.push_back(node);
                            }
                        } else {
                            String rowText;
                            for (size_t c = 0; c < row.cells.size(); c++) {
                                if (c > 0) rowText += " | ";
                                rowText += row.cells[c].content;
                            }
                            if (rowText.length() == 0) continue;
                            ContentNode node;
                            node.type = CONTENT_TEXT;
                            node.textNode.text = rowText;
                            node.textNode.style = row.cells[0].isHeader ? STYLE_BOLD : STYLE_NORMAL;
                            node.textNode.isBlockStart = true;
                            nodes.push_back(node);
                        }
                    }
                    i = tableEnd + 8;
                    nextIsBlockStart = true;
                    continue;
                }
            } else if (tag == "script" || tag == "style" || tag == "head" || tag == "figure" ||
                       tag == "svg" || tag == "figcaption") {
                int skipEnd = html.indexOf("</" + tag + ">", i);
                if(skipEnd != -1) { i = skipEnd + tag.length() + 3; continue; }
            }
            // Skip self-closing image tags
            else if (tag == "img" || tag == "image") {
                // Just skip - nothing to do for self-closing tags
            } else if (tag == "br") {
                // D3: appending "\n" here did nothing useful — the renderer's
                // word-wrap treats '\n' as plain whitespace (isspace()), so a
                // <br> never actually broke a line; poetry, addresses and
                // dialogue with <br/> ran together. The text before the <br>
                // was already flushed into its own node above (start of this
                // tag-handling block); mark the NEXT node as a new line
                // (isBlockStart) but a *soft* one so the renderer doesn't also
                // add the paragraph gap or first-line indent a real block
                // gets (see TextRenderer.cpp).
                nextIsBlockStart = true;
                nextIsSoftBreak = true;
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
        node.textNode.listNumber = currentListNumber;
        node.textNode.isBlockStart = nextIsBlockStart;
        node.textNode.softBreak = nextIsSoftBreak;
        nodes.push_back(node);
    }
    for (size_t nodeIdx = 0; nodeIdx < nodes.size(); nodeIdx++) {
        ContentNode& node = nodes[nodeIdx];
        if(node.type == CONTENT_TEXT) {
            decodeHtmlEntities(node.textNode.text);
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
            // Collapse UTF-8 to Latin-1 for the display layer. Must run AFTER
            // the punctuation replaces above (they match raw UTF-8 sequences)
            // and after decodeHtmlEntities(). TextRenderer draws these bytes
            // directly, so accented Portuguese characters depend on this.
            node.textNode.text = FontMgr::utf8ToLatin1(node.textNode.text);
            node.textNode.text.trim();
            // Filter out common image alt text placeholders
            if(node.textNode.text == "Unknown" || node.textNode.text == "image" || 
               node.textNode.text == "Image" || node.textNode.text == "[image]") {
                node.textNode.text = "";
            }

            // Heuristic: short numeric content (1-3 digits) that starts a
            // block is likely a chapter number — but only near the START of
            // the chapter (D8): the same shape (isBlockStart, 1-3 digits)
            // also matches a footnote marker, an index page number, a year
            // in a chronology, or verse numbering anywhere else in the
            // chapter, and those aren't chapter titles — promoting them to
            // a 24pt centered header was the actual bug users would see.
            // Chapter numbers live in the first node or two, so restricting
            // this to the first 3 nodes of the chapter keeps the intended
            // case (a lone "12" opening the chapter) without the false
            // positives further in.
            if (nodeIdx < 3 && node.textNode.isBlockStart && node.textNode.text.length() > 0 &&
                node.textNode.text.length() <= 3) {
                bool isNumeric = true;
                for(int i = 0; i < (int)node.textNode.text.length(); i++) {
                    if(!isdigit(node.textNode.text.charAt(i))) { isNumeric = false; break; }
                }
                if(isNumeric) {
                    node.textNode.style = STYLE_HEADER1; // Chapter number - use big centered style
                    node.textNode.align = ALIGN_CENTER;
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

// Tecto do título devolvido por getChapterTitle: um cabeçalho HTML pode, na
// prática, ser um parágrafo inteiro mal marcado (ex.: um "capitulo-titulo"
// com uma citação lá dentro), e isso não cabe numa lista de índice.
static const int BOOK32_MAX_CHAPTER_TITLE_LEN = 60;

String EpubLoader::getChapterTitle(int index) {
    return chapterTitleFromContent(getChapterContentRich(index));
}

String EpubLoader::chapterTitleFromContent(const std::vector<ContentNode>& content) {
    for (const ContentNode& node : content) {
        if (node.type != CONTENT_TEXT) continue;
        TextStyle style = node.textNode.style;
        if (style != STYLE_HEADER1 && style != STYLE_HEADER2 && style != STYLE_HEADER3 &&
            style != STYLE_HEADER4)
            continue;

        String title = node.textNode.text;
        title.trim();
        if (title.length() == 0) continue; // cabeçalho vazio: continua a procurar o próximo
        if ((int)title.length() > BOOK32_MAX_CHAPTER_TITLE_LEN) {
            title = title.substring(0, BOOK32_MAX_CHAPTER_TITLE_LEN - 3) + "...";
        }
        // node.textNode.text já passou por FontMgr::utf8ToLatin1() dentro de
        // parseHtmlToRichContent(), para o renderizador do ecrã. Este título,
        // em vez disso, acaba em JSON via /api/toc (ChapterTocStore) para a
        // UI web, por isso tem de voltar a UTF-8 — caso contrário os acentos
        // chegam ao browser como bytes Latin-1 inválidos em UTF-8 (mojibake).
        return FontMgr::latin1ToUtf8(title);
    }
    return "";
}
