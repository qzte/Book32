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
    bool isBlockStart; // Starts a new paragraph/block
    int indent;
    int listNumber; // <ol> item ordinal (1-based); 0 for <ul>/no list

    RichTextNode()
        : style(STYLE_NORMAL), align(ALIGN_LEFT), isListItem(false), isBlockStart(true), indent(0),
          listNumber(0) {}
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
    std::vector<ContentNode> getChapterContentRich(int index);  // Rich formatted content

    // Título heurístico do capítulo `index`: o texto do primeiro nó com
    // estilo de cabeçalho (h1-h4, incluindo os números de capítulo isolados
    // que a heurística de parseHtmlToRichContent já promove a cabeçalho)
    // devolvido por getChapterContentRich(). "" quando o capítulo não tem
    // nenhum cabeçalho detectável — o chamador decide o texto de recurso
    // (ver ChapterTocStore / AppReader::updateTocBuild).
    String getChapterTitle(int index);

    // Font support
    std::vector<FontInfo> getFonts();
    uint8_t* getFontData(String path, size_t* outSize);

    // Capa: href dentro do ZIP encontrado no manifest via
    // properties="cover-image" (EPUB3) ou <meta name="cover" content="ID">
    // (EPUB2, resolvido contra o manifest depois de o analisar). O tipo de
    // imagem não é guardado aqui — não se confia no media-type do OPF; quem
    // descodifica (CoverImage.h) tenta abrir como JPEG e falha em silêncio
    // se não for.
    bool hasCoverImage() {
        return coverHref.length() > 0;
    }
    // Aloca o buffer com ps_malloc/malloc, tal como getFontData(); quem chama
    // é responsável por libertá-lo. nullptr se não houver capa ou a leitura
    // do ZIP falhar.
    uint8_t* getCoverImageData(size_t* outSize);

  private:
    // Metadata
    String bookTitle;
    String bookAuthor;
    String bookPublisher;
    String bookLanguage;
    String bookPubDate;
    String bookISBN;
    
    // Paths
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
    String coverHref;                  // "" quando o manifest não indica capa nenhuma

    // Allocate UNZIP in PSRAM to avoid memory issues with the 41KB internal buffer
    UNZIP* zip;

    // Helper to parse XML for specific attribute
    String extractAttribute(const String& xml, const String& tag, const String& attr);
    // Helper to get text content of tag
    String extractTagContent(const String& xml, const String& tag);
    // Helper to extract metadata from OPF
    String extractMetadata(const String& xml, const String& tag);

    // Helper to read file from zip
    String readFileFromZip(const char* path);
    
    // Rich content parsing
    std::vector<ContentNode> parseHtmlToRichContent(const String& html);
    Table parseTable(const String& tableHtml);
    TextStyle getStyleFromTag(String tag);
    TextAlign getAlignFromStyle(String styleAttr);

    bool parseContainer();
    bool parseOpf();
};

#endif
