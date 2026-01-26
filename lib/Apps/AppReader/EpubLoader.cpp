#include "EpubLoader.h"
#include <LittleFS.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

// Define Return Codes if missing
#ifndef ZIP_SUCCESS
#define ZIP_SUCCESS 0
#endif

// Use low-level POSIX file I/O to avoid ESP32-S3 Arduino File class issues
static int zipFd = -1;

void *myOpen(const char *filename, int32_t *size) {
    Serial.printf("ZIP myOpen: %s\n", filename);
    Serial.printf("Free heap: %d, PSRAM: %d\n", ESP.getFreeHeap(), ESP.getFreePsram());

    // Close any previously open file
    if (zipFd >= 0) {
        close(zipFd);
        zipFd = -1;
    }

    // LittleFS mounts at /littlefs, so prepend if not already
    String fullPath = filename;
    if (!fullPath.startsWith("/littlefs")) {
        fullPath = "/littlefs" + fullPath;
    }

    Serial.printf("ZIP opening: %s\n", fullPath.c_str());

    zipFd = open(fullPath.c_str(), O_RDONLY);
    if (zipFd < 0) {
        Serial.printf("ZIP myOpen: failed to open, errno=%d\n", errno);
        return NULL;
    }

    // Get file size using fstat
    struct stat st;
    if (fstat(zipFd, &st) != 0) {
        Serial.printf("ZIP myOpen: fstat failed, errno=%d\n", errno);
        close(zipFd);
        zipFd = -1;
        return NULL;
    }

    *size = st.st_size;
    Serial.printf("ZIP myOpen: success, fd=%d, size=%d\n", zipFd, *size);

    // Return the file descriptor as the handle (offset by 1 to avoid returning 0)
    return (void*)(intptr_t)(zipFd + 1);
}

void myClose(void *p) {
    Serial.println("ZIP myClose");
    if (zipFd >= 0) {
        close(zipFd);
        zipFd = -1;
    }
}

int32_t myRead(void *p, uint8_t *buffer, int32_t length) {
    if (zipFd < 0 || !buffer || length <= 0) return -1;

    ssize_t bytesRead = read(zipFd, buffer, length);
    if (bytesRead < 0) {
        Serial.printf("ZIP myRead: error, errno=%d\n", errno);
        return -1;
    }
    return (int32_t)bytesRead;
}

int32_t mySeek(void *p, int32_t position, int iType) {
    if (zipFd < 0) return -1;

    off_t newPos = lseek(zipFd, position, iType);
    if (newPos < 0) {
        Serial.printf("ZIP mySeek: error, errno=%d\n", errno);
        return -1;
    }
    return (int32_t)newPos;
}

EpubLoader::EpubLoader() {
    // Allocate UNZIP in PSRAM - the structure contains a 41KB buffer
    // that can cause issues if allocated on regular heap
    zip = (UNZIP*)ps_malloc(sizeof(UNZIP));
    if (!zip) {
        Serial.println("EpubLoader: PSRAM alloc failed, using heap");
        zip = new UNZIP();
    } else {
        // Initialize the object in place
        new (zip) UNZIP();
        Serial.println("EpubLoader: UNZIP allocated in PSRAM");
    }
}

EpubLoader::~EpubLoader() {
    if (zip) {
        zip->~UNZIP();
        free(zip);
        zip = nullptr;
    }
}

bool EpubLoader::open(const char* path) {
    Serial.printf("EpubLoader::open(%s)\n", path);
    Serial.printf("Free heap before open: %d, PSRAM: %d\n", ESP.getFreeHeap(), ESP.getFreePsram());

    epubPath = String(path);

    Serial.println("Calling zip->openZIP...");
    int result = zip->openZIP(path, myOpen, myClose, myRead, mySeek);
    Serial.printf("zip->openZIP returned: %d\n", result);

    if (result != ZIP_SUCCESS) {
        Serial.println("Failed to open ZIP");
        return false;
    }

    Serial.println("Parsing container.xml...");
    if (!parseContainer()) {
        Serial.println("Failed to parse container");
        close();
        return false;
    }

    Serial.println("Parsing OPF...");
    if (!parseOpf()) {
        Serial.println("Failed to parse OPF");
        close();
        return false;
    }

    Serial.printf("EPUB opened successfully. Chapters: %d\n", spine.size());
    Serial.printf("Free heap after open: %d, PSRAM: %d\n", ESP.getFreeHeap(), ESP.getFreePsram());
    return true;
}

void EpubLoader::close() {
    if (zip) {
        zip->closeZIP();
    }
    spine.clear();
    manifest.clear();
}

String EpubLoader::getTitle() {
    return bookTitle.length() > 0 ? bookTitle : "Unknown";
}

int EpubLoader::getChapterCount() {
    return spine.size();
}

String EpubLoader::getChapterContent(int index) {
    if(index < 0 || index >= spine.size()) return "";

    Serial.printf("getChapterContent: index=%d\n", index);

    // Construct full path
    String href = spine[index].href;
    String fullPath = rootDir + href;

    // Normalize path
    if(fullPath.startsWith("./")) fullPath = fullPath.substring(2);

    String content = readFileFromZip(fullPath.c_str());
    Serial.printf("Raw Content Length: %d\n", content.length());

    // Improved HTML to plain text conversion
    String clean;
    clean.reserve(content.length() / 2);  // Pre-allocate for efficiency

    bool inTag = false;
    bool skipContent = false;  // For skipping script, style, image content
    String currentTag;

    for(int i = 0; i < content.length(); i++) {
        char c = content.charAt(i);

        if(c == '<') {
            inTag = true;
            currentTag = "";

            // Look ahead to get the tag name
            int j = i + 1;
            while(j < content.length() && content.charAt(j) != '>' && content.charAt(j) != ' ' && j - i < 20) {
                currentTag += (char)tolower(content.charAt(j));
                j++;
            }

            // Check for tags that add line breaks (block elements)
            if(currentTag == "p" || currentTag == "/p" ||
               currentTag == "div" || currentTag == "/div" ||
               currentTag == "br" || currentTag == "br/") {
                // Add newline for paragraph/block elements
                if(clean.length() > 0 && clean.charAt(clean.length()-1) != '\n') {
                    clean += "\n";
                }
                // Add extra newline after closing paragraph for spacing
                if(currentTag == "/p") {
                    clean += "\n";
                }
            }
            // Headings get extra spacing
            else if(currentTag.startsWith("h") && currentTag.length() == 2 ||
                    currentTag.startsWith("/h") && currentTag.length() == 3) {
                if(clean.length() > 0 && clean.charAt(clean.length()-1) != '\n') {
                    clean += "\n";
                }
                if(currentTag.startsWith("/h")) {
                    clean += "\n";  // Extra line after heading
                }
            }
            // List items
            else if(currentTag == "li") {
                if(clean.length() > 0 && clean.charAt(clean.length()-1) != '\n') {
                    clean += "\n";
                }
                clean += "• ";  // Bullet point for list items
            }
            else if(currentTag == "/li") {
                if(clean.length() > 0 && clean.charAt(clean.length()-1) != '\n') {
                    clean += "\n";
                }
            }
            // Skip content inside these tags entirely
            else if(currentTag == "script" || currentTag == "style" ||
                    currentTag == "img" || currentTag == "image" || currentTag == "svg" ||
                    currentTag == "head" || currentTag == "title") {
                skipContent = true;
            }
            else if(currentTag == "/script" || currentTag == "/style" || currentTag == "/svg" ||
                    currentTag == "/head" || currentTag == "/title") {
                skipContent = false;
            }

        } else if (c == '>') {
            inTag = false;
            // For self-closing image tags, don't skip content after
            if(currentTag == "img" || currentTag == "image") {
                skipContent = false;
            }
        } else if (!inTag && !skipContent) {
            // Convert whitespace
            if(c == '\n' || c == '\r' || c == '\t') {
                c = ' ';
            }
            // Collapse multiple spaces
            if(c == ' ' && clean.length() > 0 && clean.charAt(clean.length()-1) == ' ') {
                continue;
            }
            // Don't add leading spaces at start of lines
            if(c == ' ' && clean.length() > 0 && clean.charAt(clean.length()-1) == '\n') {
                continue;
            }
            clean += c;
        }
    }

    // Decode UTF-8 special characters (these appear as raw bytes in EPUBs)
    // Em-dash (—) UTF-8: 0xE2 0x80 0x94
    clean.replace("\xE2\x80\x94", "--");
    // En-dash (–) UTF-8: 0xE2 0x80 0x93
    clean.replace("\xE2\x80\x93", "-");
    // Left double quote (") UTF-8: 0xE2 0x80 0x9C
    clean.replace("\xE2\x80\x9C", "\"");
    // Right double quote (") UTF-8: 0xE2 0x80 0x9D
    clean.replace("\xE2\x80\x9D", "\"");
    // Left single quote (') UTF-8: 0xE2 0x80 0x98
    clean.replace("\xE2\x80\x98", "'");
    // Right single quote (') UTF-8: 0xE2 0x80 0x99
    clean.replace("\xE2\x80\x99", "'");
    // Ellipsis (…) UTF-8: 0xE2 0x80 0xA6
    clean.replace("\xE2\x80\xA6", "...");
    // Non-breaking space UTF-8: 0xC2 0xA0
    clean.replace("\xC2\xA0", " ");
    // Bullet (•) UTF-8: 0xE2 0x80 0xA2
    clean.replace("\xE2\x80\xA2", "*");

    // Decode HTML entities (fallback for entity-encoded content)
    clean.replace("&nbsp;", " ");
    clean.replace("&#160;", " ");
    clean.replace("&lt;", "<");
    clean.replace("&gt;", ">");
    clean.replace("&amp;", "&");
    clean.replace("&quot;", "\"");
    clean.replace("&apos;", "'");
    clean.replace("&#39;", "'");
    clean.replace("&mdash;", "--");
    clean.replace("&ndash;", "-");
    clean.replace("&#8212;", "--");
    clean.replace("&#8211;", "-");
    clean.replace("&hellip;", "...");
    clean.replace("&#8230;", "...");
    clean.replace("&ldquo;", "\"");
    clean.replace("&rdquo;", "\"");
    clean.replace("&#8220;", "\"");
    clean.replace("&#8221;", "\"");
    clean.replace("&lsquo;", "'");
    clean.replace("&rsquo;", "'");
    clean.replace("&#8216;", "'");
    clean.replace("&#8217;", "'");

    // Clean up excessive newlines (more than 2 consecutive)
    while(clean.indexOf("\n\n\n") != -1) {
        clean.replace("\n\n\n", "\n\n");
    }

    // Remove common artifacts that appear in EPUBs
    // These often appear from cover pages or metadata
    clean.replace("\nUnknown\n", "\n");
    clean.replace("\nunknown\n", "\n");
    if(clean.startsWith("Unknown\n")) {
        clean = clean.substring(8);
    }
    if(clean.startsWith("unknown\n")) {
        clean = clean.substring(8);
    }

    // Trim leading/trailing whitespace
    clean.trim();

    Serial.printf("Clean Content Length: %d\n", clean.length());
    return clean;
}

bool EpubLoader::parseContainer() {
    Serial.println("parseContainer: reading META-INF/container.xml");
    String xml = readFileFromZip("META-INF/container.xml");
    Serial.printf("parseContainer: xml length=%d\n", xml.length());

    if(xml.length() == 0) {
        Serial.println("parseContainer: empty xml");
        return false;
    }

    opfPath = extractAttribute(xml, "rootfile", "full-path");
    Serial.printf("parseContainer: opfPath=%s\n", opfPath.c_str());

    if(opfPath.length() == 0) {
        Serial.println("parseContainer: no full-path found");
        return false;
    }

    int lastSlash = opfPath.lastIndexOf('/');
    if(lastSlash != -1) {
        rootDir = opfPath.substring(0, lastSlash + 1);
    } else {
        rootDir = "";
    }
    Serial.printf("parseContainer: OPF Path: %s, RootDir: %s\n", opfPath.c_str(), rootDir.c_str());
    return true;
}

bool EpubLoader::parseOpf() {
    Serial.println("EpubLoader::parseOpf");
    String xml = readFileFromZip(opfPath.c_str());
    Serial.printf("OPF XML Size: %d\n", xml.length());
    if(xml.length() == 0) return false;

    // Extract book title from <dc:title> or <title>
    bookTitle = extractMetadata(xml, "dc:title");
    if(bookTitle.length() == 0) {
        bookTitle = extractMetadata(xml, "title");
    }
    Serial.printf("Book title: %s\n", bookTitle.c_str());

    // Extract author from <dc:creator>
    bookAuthor = extractMetadata(xml, "dc:creator");
    Serial.printf("Book author: %s\n", bookAuthor.c_str());

    // Extract publisher from <dc:publisher>
    bookPublisher = extractMetadata(xml, "dc:publisher");
    Serial.printf("Book publisher: %s\n", bookPublisher.c_str());

    // Extract language from <dc:language>
    bookLanguage = extractMetadata(xml, "dc:language");
    Serial.printf("Book language: %s\n", bookLanguage.c_str());

    // Extract publication date from <dc:date>
    bookPubDate = extractMetadata(xml, "dc:date");
    Serial.printf("Book publication date: %s\n", bookPubDate.c_str());

    // Extract ISBN from <dc:identifier> with opf:scheme="ISBN" or containing "isbn"
    int identifierPos = 0;
    while(true) {
        int idStart = xml.indexOf("<dc:identifier", identifierPos);
        if(idStart == -1) break;
        int idEnd = xml.indexOf("</dc:identifier>", idStart);
        if(idEnd == -1) break;

        String identifierBlock = xml.substring(idStart, idEnd + 16);
        if(identifierBlock.indexOf("ISBN") != -1 || identifierBlock.indexOf("isbn") != -1) {
            int contentStart = identifierBlock.indexOf(">") + 1;
            int contentEnd = identifierBlock.indexOf("</");
            if(contentStart != -1 && contentEnd != -1) {
                bookISBN = identifierBlock.substring(contentStart, contentEnd);
                bookISBN.trim();
                Serial.printf("Book ISBN: %s\n", bookISBN.c_str());
                break;
            }
        }
        identifierPos = idEnd;
    }

    // Look for cover image in metadata: <meta name="cover" content="cover-id"/>
    String coverId = "";
    int metaPos = 0;
    while(true) {
        int metaStart = xml.indexOf("<meta", metaPos);
        if(metaStart == -1) break;
        int metaEnd = xml.indexOf(">", metaStart);
        if(metaEnd == -1) break;

        String metaTag = xml.substring(metaStart, metaEnd + 1);
        if(metaTag.indexOf("name=\"cover\"") != -1 || metaTag.indexOf("name='cover'") != -1) {
            coverId = extractAttribute(metaTag, "meta", "content");
            Serial.printf("Found cover meta, id=%s\n", coverId.c_str());
            break;
        }
        metaPos = metaEnd;
    }

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

            // Check for fonts (.ttf, .otf, .woff, .woff2)
            String hrefLower = href;
            hrefLower.toLowerCase();
            if(hrefLower.endsWith(".ttf") || hrefLower.endsWith(".otf") ||
               hrefLower.endsWith(".woff") || hrefLower.endsWith(".woff2") ||
               mediaType.indexOf("font") != -1) {
                FontInfo font;
                font.path = rootDir + href;
                
                // Determine format
                if(hrefLower.endsWith(".ttf")) font.format = "ttf";
                else if(hrefLower.endsWith(".otf")) font.format = "otf";
                else if(hrefLower.endsWith(".woff")) font.format = "woff";
                else if(hrefLower.endsWith(".woff2")) font.format = "woff2";
                
                // Try to determine font family from filename
                int lastSlash = href.lastIndexOf('/');
                int lastDot = href.lastIndexOf('.');
                if(lastSlash != -1 && lastDot != -1) {
                    font.family = href.substring(lastSlash + 1, lastDot);
                } else if(lastDot != -1) {
                    font.family = href.substring(0, lastDot);
                }
                
                // Determine style from filename
                String filenameLower = font.family;
                filenameLower.toLowerCase();
                if(filenameLower.indexOf("bolditalic") != -1 || filenameLower.indexOf("bold-italic") != -1) {
                    font.style = "bold-italic";
                } else if(filenameLower.indexOf("bold") != -1) {
                    font.style = "bold";
                } else if(filenameLower.indexOf("italic") != -1) {
                    font.style = "italic";
                } else {
                    font.style = "normal";
                }
                
                fonts.push_back(font);
                Serial.printf("Found font: %s (%s, %s)\n", font.family.c_str(), font.style.c_str(), font.format.c_str());
            }

            // Check for cover image
            // Method 1: Match the cover id from metadata
            if(coverId.length() > 0 && id == coverId) {
                coverPath = rootDir + href;
                Serial.printf("Cover found via meta: %s\n", coverPath.c_str());
            }
            // Method 2: EPUB3 properties="cover-image"
            if(itemTag.indexOf("properties=\"cover-image\"") != -1 ||
               itemTag.indexOf("properties='cover-image'") != -1) {
                coverPath = rootDir + href;
                Serial.printf("Cover found via properties: %s\n", coverPath.c_str());
            }
            // Method 3: Common cover filenames (fallback)
            if(coverPath.length() == 0) {
                if(hrefLower.indexOf("cover") != -1 &&
                   (hrefLower.endsWith(".jpg") || hrefLower.endsWith(".jpeg") || hrefLower.endsWith(".png"))) {
                    coverPath = rootDir + href;
                    Serial.printf("Cover found via filename: %s\n", coverPath.c_str());
                }
            }
        }
        pos = itemEnd;
    }

    int spineStart = xml.indexOf("<spine");
    int spineEnd = xml.indexOf("</spine>");
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
            SpineItem item;
            item.id = idref;
            item.href = manifest[idref];
            spine.push_back(item);
        }
        pos = itemRefEnd;
    }

    Serial.printf("Spine Items: %d, Cover: %s, Fonts: %d\n", spine.size(), coverPath.c_str(), fonts.size());
    return true;
}

String EpubLoader::getCoverPath() {
    return coverPath;
}

uint8_t* EpubLoader::getCoverData(size_t* outSize) {
    if(coverPath.length() == 0) {
        *outSize = 0;
        return nullptr;
    }

    Serial.printf("Loading cover: %s\n", coverPath.c_str());

    if (zip->locateFile(coverPath.c_str()) != 0) {
        Serial.println("Cover file not found in zip");
        *outSize = 0;
        return nullptr;
    }

    if (zip->openCurrentFile() != 0) {
        Serial.println("Failed to open cover file");
        *outSize = 0;
        return nullptr;
    }

    unz_file_info fileInfo;
    char szName[256];
    zip->getFileInfo(&fileInfo, szName, sizeof(szName), NULL, 0, NULL, 0);
    size_t size = fileInfo.uncompressed_size;

    if(size <= 0 || size > 500 * 1024) {  // Max 500KB for cover
        Serial.printf("Cover file invalid size: %d\n", size);
        zip->closeCurrentFile();
        *outSize = 0;
        return nullptr;
    }

    uint8_t* buffer = (uint8_t*)ps_malloc(size);
    if(!buffer) {
        buffer = (uint8_t*)malloc(size);
    }
    if(!buffer) {
        Serial.println("Failed to allocate cover buffer");
        zip->closeCurrentFile();
        *outSize = 0;
        return nullptr;
    }

    zip->readCurrentFile(buffer, size);
    zip->closeCurrentFile();

    *outSize = size;
    Serial.printf("Cover loaded: %d bytes\n", size);
    return buffer;
}

String EpubLoader::extractAttribute(String xml, String tag, String attr) {
    int attrStart = xml.indexOf(attr + "=\"");
    if(attrStart == -1) {
        attrStart = xml.indexOf(attr + "='");
    }
    if(attrStart == -1) return "";
    
    int valStart = attrStart + attr.length() + 2; 
    char quote = xml.charAt(attrStart + attr.length() + 1); 
    
    int valEnd = xml.indexOf(quote, valStart);
    if(valEnd == -1) return "";
    
    return xml.substring(valStart, valEnd);
}

String EpubLoader::extractTagContent(String xml, String tag) {
    int start = xml.indexOf("<" + tag);
    if(start == -1) return "";
    start = xml.indexOf(">", start) + 1;
    int end = xml.indexOf("</" + tag);
    if(end == -1) return "";
    return xml.substring(start, end);
}

String EpubLoader::readFileFromZip(const char* path) {
    Serial.printf("readFileFromZip: %s\n", path);

    // Locate File
    if (zip->locateFile(path) != ZIP_SUCCESS) {
         Serial.printf("File not found in zip: %s\n", path);
         return "";
    }

    // Open Current File
    if (zip->openCurrentFile() != ZIP_SUCCESS) {
        Serial.println("Failed to open current file");
        return "";
    }

    // Get Info to know size
    unz_file_info fileInfo;
    char szName[256];
    zip->getFileInfo(&fileInfo, szName, sizeof(szName), NULL, 0, NULL, 0);
    int size = fileInfo.uncompressed_size;
    Serial.printf("File: %s, Size: %d\n", szName, size);

    if(size <= 0 || size > 1024 * 1024) {
        Serial.println("File invalid size");
        zip->closeCurrentFile();
        return "";
    }

    // Allocate in PSRAM (ESP32-S3 has 8MB)
    uint8_t *buffer = (uint8_t*)ps_malloc(size + 1);
    if(!buffer) {
        Serial.println("PSRAM alloc failed, trying heap");
        buffer = (uint8_t*)malloc(size + 1);
    }
    if(!buffer) {
        Serial.println("Alloc failed for file");
        zip->closeCurrentFile();
        return "";
    }

    zip->readCurrentFile(buffer, size);
    buffer[size] = 0; // Null terminate

    Serial.printf("Read complete: %d bytes\n", size);
    String str = String((char*)buffer);
    free(buffer);

    zip->closeCurrentFile();
    return str;
}

// Metadata getters
String EpubLoader::getAuthor() {
    return bookAuthor.length() > 0 ? bookAuthor : "Unknown";
}

String EpubLoader::getPublisher() {
    return bookPublisher.length() > 0 ? bookPublisher : "Unknown";
}

String EpubLoader::getLanguage() {
    return bookLanguage.length() > 0 ? bookLanguage : "en";
}

String EpubLoader::getPublicationDate() {
    return bookPubDate.length() > 0 ? bookPubDate : "Unknown";
}

String EpubLoader::getISBN() {
    return bookISBN.length() > 0 ? bookISBN : "Unknown";
}

// Font support
std::vector<FontInfo> EpubLoader::getFonts() {
    return fonts;
}

uint8_t* EpubLoader::getFontData(String path, size_t* outSize) {
    if(path.length() == 0) {
        *outSize = 0;
        return nullptr;
    }

    Serial.printf("Loading font: %s\n", path.c_str());

    if (zip->locateFile(path.c_str()) != 0) {
        Serial.println("Font file not found in zip");
        *outSize = 0;
        return nullptr;
    }

    if (zip->openCurrentFile() != 0) {
        Serial.println("Failed to open font file");
        *outSize = 0;
        return nullptr;
    }

    unz_file_info fileInfo;
    char szName[256];
    zip->getFileInfo(&fileInfo, szName, sizeof(szName), NULL, 0, NULL, 0);
    size_t size = fileInfo.uncompressed_size;

    if(size <= 0 || size > 2 * 1024 * 1024) {  // Max 2MB for fonts
        Serial.printf("Font file invalid size: %d\n", size);
        zip->closeCurrentFile();
        *outSize = 0;
        return nullptr;
    }

    uint8_t* buffer = (uint8_t*)ps_malloc(size);
    if(!buffer) {
        buffer = (uint8_t*)malloc(size);
    }
    if(!buffer) {
        Serial.println("Failed to allocate font buffer");
        zip->closeCurrentFile();
        *outSize = 0;
        return nullptr;
    }

    zip->readCurrentFile(buffer, size);
    zip->closeCurrentFile();

    *outSize = size;
    Serial.printf("Font loaded: %d bytes\n", size);
    return buffer;
}

// Helper to extract metadata from OPF XML
String EpubLoader::extractMetadata(String xml, String tag) {
    int tagStart = xml.indexOf("<" + tag);
    if(tagStart == -1) return "";
    
    int tagEnd = xml.indexOf(">", tagStart);
    if(tagEnd == -1) return "";
    
    int contentEnd = xml.indexOf("</" + tag + ">", tagEnd);
    if(contentEnd == -1) {
        // Try without namespace
        contentEnd = xml.indexOf("</", tagEnd);
    }
    if(contentEnd == -1) return "";
    
    String content = xml.substring(tagEnd + 1, contentEnd);
    content.trim();
    return content;
}

// Get text style from HTML tag
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

// Get text alignment from CSS style attribute
TextAlign EpubLoader::getAlignFromStyle(String styleAttr) {
    styleAttr.toLowerCase();
    
    if(styleAttr.indexOf("text-align:center") != -1 || styleAttr.indexOf("text-align: center") != -1) {
        return ALIGN_CENTER;
    }
    if(styleAttr.indexOf("text-align:right") != -1 || styleAttr.indexOf("text-align: right") != -1) {
        return ALIGN_RIGHT;
    }
    if(styleAttr.indexOf("text-align:justify") != -1 || styleAttr.indexOf("text-align: justify") != -1) {
        return ALIGN_JUSTIFY;
    }
    
    return ALIGN_LEFT;
}

// Parse HTML table
Table EpubLoader::parseTable(String tableHtml) {
    Table table;
    
    // Find all <tr> tags
    int trPos = 0;
    while(true) {
        int trStart = tableHtml.indexOf("<tr", trPos);
        if(trStart == -1) break;
        
        int trEnd = tableHtml.indexOf("</tr>", trStart);
        if(trEnd == -1) break;
        
        String rowHtml = tableHtml.substring(trStart, trEnd + 5);
        TableRow row;
        
        // Find all <td> and <th> tags in this row
        int cellPos = 0;
        while(true) {
            int tdStart = rowHtml.indexOf("<td", cellPos);
            int thStart = rowHtml.indexOf("<th", cellPos);
            
            int cellStart = -1;
            bool isHeader = false;
            
            if(tdStart != -1 && (thStart == -1 || tdStart < thStart)) {
                cellStart = tdStart;
                isHeader = false;
            } else if(thStart != -1) {
                cellStart = thStart;
                isHeader = true;
            }
            
            if(cellStart == -1) break;
            
            String cellTag = isHeader ? "th" : "td";
            int cellTagEnd = rowHtml.indexOf(">", cellStart);
            int cellEnd = rowHtml.indexOf("</" + cellTag + ">", cellTagEnd);
            
            if(cellTagEnd == -1 || cellEnd == -1) break;
            
            TableCell cell;
            cell.isHeader = isHeader;
            
            // Extract cell attributes (colspan, rowspan)
            String cellOpenTag = rowHtml.substring(cellStart, cellTagEnd + 1);
            String colspanStr = extractAttribute(cellOpenTag, cellTag, "colspan");
            String rowspanStr = extractAttribute(cellOpenTag, cellTag, "rowspan");
            
            if(colspanStr.length() > 0) cell.colspan = colspanStr.toInt();
            if(rowspanStr.length() > 0) cell.rowspan = rowspanStr.toInt();
            
            // Extract cell content (strip HTML tags)
            String cellContent = rowHtml.substring(cellTagEnd + 1, cellEnd);
            
            // Simple HTML stripping for cell content
            String clean;
            bool inTag = false;
            for(int i = 0; i < cellContent.length(); i++) {
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
            if((int)row.cells.size() > table.columnCount) {
                table.columnCount = row.cells.size();
            }
        }
        
        trPos = trEnd + 5;
    }
    
    return table;
}

// Parse HTML to rich content with formatting
std::vector<ContentNode> EpubLoader::parseHtmlToRichContent(String html) {
    std::vector<ContentNode> nodes;
    
    // Stack to track current formatting
    std::vector<TextStyle> styleStack;
    styleStack.push_back(STYLE_NORMAL);
    
    TextAlign currentAlign = ALIGN_LEFT;
    bool isListItem = false;
    
    String currentText;
    
    int i = 0;
    while(i < html.length()) {
        char c = html.charAt(i);
        
        if(c == '<') {
            // Save current text if any
            if(currentText.length() > 0) {
                currentText.trim();
                if(currentText.length() > 0) {
                    ContentNode node;
                    node.type = CONTENT_TEXT;
                    node.textNode.text = currentText;
                    node.textNode.style = styleStack.back();
                    node.textNode.align = currentAlign;
                    node.textNode.isListItem = isListItem;
                    nodes.push_back(node);
                    currentText = "";
                    isListItem = false;
                }
            }
            
            // Find tag end
            int tagEnd = html.indexOf('>', i);
            if(tagEnd == -1) break;
            
            String fullTag = html.substring(i, tagEnd + 1);
            String tag;
            
            // Extract tag name
            int spacePos = fullTag.indexOf(' ');
            int closePos = fullTag.indexOf('>');
            if(spacePos != -1 && spacePos < closePos) {
                tag = fullTag.substring(1, spacePos);
            } else {
                tag = fullTag.substring(1, closePos);
            }
            
            tag.toLowerCase();
            bool isClosing = tag.startsWith("/");
            if(isClosing) tag = tag.substring(1);
            
            // Handle formatting tags
            if(tag == "b" || tag == "strong" || tag == "i" || tag == "em" ||
               tag == "h1" || tag == "h2" || tag == "h3" || tag == "h4") {
                if(!isClosing) {
                    TextStyle newStyle = getStyleFromTag(tag);
                    styleStack.push_back(newStyle);
                } else {
                    if(styleStack.size() > 1) styleStack.pop_back();
                }
            }
            // Handle alignment from style attribute
            else if((tag == "p" || tag == "div") && !isClosing) {
                String styleAttr = extractAttribute(fullTag, tag, "style");
                if(styleAttr.length() > 0) {
                    currentAlign = getAlignFromStyle(styleAttr);
                }
            }
            // Handle list items
            else if(tag == "li" && !isClosing) {
                isListItem = true;
            }
            // Handle tables
            else if(tag == "table" && !isClosing) {
                // Find table end
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
                    continue;
                }
            }
            // Skip script, style, head tags
            else if(tag == "script" || tag == "style" || tag == "head") {
                int skipEnd = html.indexOf("</" + tag + ">", i);
                if(skipEnd != -1) {
                    i = skipEnd + tag.length() + 3;
                    continue;
                }
            }
            // Add line breaks for block elements
            else if(tag == "br" || tag == "/p" || tag == "/div" || tag == "/h1" ||
                    tag == "/h2" || tag == "/h3" || tag == "/h4" || tag == "/li") {
                currentText += "\n";
            }
            
            i = tagEnd + 1;
        } else {
            // Regular text character
            if(c == '\n' || c == '\r' || c == '\t') {
                c = ' ';
            }
            // Collapse multiple spaces
            if(c == ' ' && currentText.length() > 0 && currentText.charAt(currentText.length()-1) == ' ') {
                i++;
                continue;
            }
            currentText += c;
            i++;
        }
    }
    
    // Save any remaining text
    if(currentText.length() > 0) {
        currentText.trim();
        if(currentText.length() > 0) {
            ContentNode node;
            node.type = CONTENT_TEXT;
            node.textNode.text = currentText;
            node.textNode.style = styleStack.back();
            node.textNode.align = currentAlign;
            node.textNode.isListItem = isListItem;
            nodes.push_back(node);
        }
    }
    
    return nodes;
}

// Get chapter content with rich formatting
std::vector<ContentNode> EpubLoader::getChapterContentRich(int index) {
    if(index < 0 || index >= spine.size()) {
        return std::vector<ContentNode>();
    }

    Serial.printf("getChapterContentRich: index=%d\n", index);

    // Construct full path
    String href = spine[index].href;
    String fullPath = rootDir + href;

    // Normalize path
    if(fullPath.startsWith("./")) fullPath = fullPath.substring(2);

    String content = readFileFromZip(fullPath.c_str());
    Serial.printf("Raw Content Length: %d\n", content.length());

    // Parse HTML to rich content
    std::vector<ContentNode> nodes = parseHtmlToRichContent(content);
    
    Serial.printf("Parsed %d content nodes\n", nodes.size());
    return nodes;
}

