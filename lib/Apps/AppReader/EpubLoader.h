#ifndef EPUB_LOADER_H
#define EPUB_LOADER_H

#include <Arduino.h>
#include <vector>
#include <map>
#include <unzipLIB.h>

// Text formatting enums
enum TextStyle {
    STYLE_NORMAL,
    STYLE_BOLD,
    STYLE_ITALIC,
    STYLE_BOLD_ITALIC,
    STYLE_HEADER1,
    STYLE_HEADER2,
    STYLE_HEADER3,
    STYLE_HEADER4
};

enum TextAlign {
    ALIGN_LEFT,
    ALIGN_CENTER,
    ALIGN_RIGHT,
    ALIGN_JUSTIFY
};

// Rich text node for formatted content
struct RichTextNode {
    String text;
    TextStyle style;
    TextAlign align;
    bool isListItem;
    int indent; // Indentation in pixels (or relative units)
    
    RichTextNode() : style(STYLE_NORMAL), align(ALIGN_LEFT), isListItem(false), indent(0) {}
};

// Table structures
struct TableCell {
    String content;
    int colspan;
    int rowspan;
    bool isHeader;
    
    TableCell() : colspan(1), rowspan(1), isHeader(false) {}
};

struct TableRow {
    std::vector<TableCell> cells;
};

struct Table {
    std::vector<TableRow> rows;
    int columnCount;
    
    Table() : columnCount(0) {}
};

// Font metadata
struct FontInfo {
    String family;
    String path;
    String style;  // normal, italic, bold, bold-italic
    String format; // ttf, otf, woff, woff2
};

// Content node - can be text or table
enum ContentType {
    CONTENT_TEXT,
    CONTENT_TABLE
};

struct ContentNode {
    ContentType type;
    RichTextNode textNode;
    Table table;
    
    ContentNode() : type(CONTENT_TEXT) {}
};

class EpubLoader {
public:
    EpubLoader();
    ~EpubLoader();
    bool open(const char* path);
    void close();
    
    // Metadata getters
    String getTitle();
    String getAuthor();
    String getPublisher();
    String getLanguage();
    String getPublicationDate();
    String getISBN();
    
    // Content getters
    int getChapterCount();
    String getChapterContent(int index);  // Legacy plain text
    std::vector<ContentNode> getChapterContentRich(int index);  // Rich formatted content
    
    // Font support
    std::vector<FontInfo> getFonts();
    uint8_t* getFontData(String path, size_t* outSize);

    // Cover image support
    String getCoverPath();  // Returns path to cover image inside EPUB
    uint8_t* getCoverData(size_t* outSize);  // Returns raw image bytes (caller must free)

private:
    // Metadata
    String bookTitle;
    String bookAuthor;
    String bookPublisher;
    String bookLanguage;
    String bookPubDate;
    String bookISBN;
    
    // Paths
    String coverPath;  // Path to cover image inside EPUB
    String epubPath;
    String opfPath;
    String rootDir; // Directory of the OPF file
    
    // Fonts
    std::vector<FontInfo> fonts;

    struct SpineItem {
        String id;
        String href;
    };

    std::vector<SpineItem> spine;
    std::map<String, String> manifest; // id -> href

    // Allocate UNZIP in PSRAM to avoid memory issues with the 41KB internal buffer
    UNZIP* zip;

    // Helper to parse XML for specific attribute
    String extractAttribute(String xml, String tag, String attr);
    // Helper to get text content of tag
    String extractTagContent(String xml, String tag);
    // Helper to extract metadata from OPF
    String extractMetadata(String xml, String tag);

    // Helper to read file from zip
    String readFileFromZip(const char* path);
    
    // Rich content parsing
    std::vector<ContentNode> parseHtmlToRichContent(String html);
    Table parseTable(String tableHtml);
    TextStyle getStyleFromTag(String tag);
    TextAlign getAlignFromStyle(String styleAttr);

    bool parseContainer();
    bool parseOpf();
};

#endif
