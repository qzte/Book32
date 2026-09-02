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
    // true when isBlockStart is true because of a <br> rather than a real
    // block element (<p>/<div>/<hN>/<li>): the renderer still starts a new
    // line for it, but skips the paragraph gap and first-line indent a real
    // block gets (see D3).
    bool softBreak;
    int indent;
    int listNumber; // <ol> item ordinal (1-based); 0 for <ul>/no list

    RichTextNode()
        : style(STYLE_NORMAL), align(ALIGN_LEFT), isListItem(false), isBlockStart(true), softBreak(false),
          indent(0), listNumber(0) {}
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
    // (ver ChapterTocStore / AppReader::updateIndexing). Reabre e reanalisa
    // o capítulo a cada chamada — usar chapterTitleFromContent() em vez
    // disto quando o conteúdo já tiver sido lido por outra razão (ver D6 da
    // avaliação de código do eReader).
    String getChapterTitle(int index);

    // Mesma heurística de getChapterTitle(), mas sem custo de I/O: opera
    // sobre um vector<ContentNode> já analisado em vez de reabrir o ZIP.
    // Estática, sem estado de instância nenhum — mas ainda depende de String
    // e FontMgr::latin1ToUtf8(), por isso não é host-testável sem o stub de
    // Arduino.h que falta ao parser (ver secção "Testabilidade" da
    // avaliação). getChapterTitle(index) é só getChapterContentRich(index)
    // seguido desta chamada.
    static String chapterTitleFromContent(const std::vector<ContentNode>& content);

    // false quando o capítulo `index` está referenciado no <guide> do OPF
    // (EPUB2) com um tipo não-narrativo (capa, índice, página de rosto,
    // créditos, dedicatória, etc. — ver isNarrativeGuideType em
    // EpubLoader.cpp). true por omissão: um EPUB sem <guide> (comum em
    // EPUB3 puro, que usa antes um nav.xhtml de landmarks ainda não lido
    // aqui) ou um índice fora do intervalo da spine mantém-se narrativo,
    // que é o comportamento de sempre. Não muda getChapterCount() nem a
    // ordem/índices da spine — só classifica; ver
    // docs/plans/2026-09-01-filtrar-capitulos-nao-narrativos-design.md.
    bool isChapterNarrative(int index) const;

    // Tipo bruto de <guide><reference type="..."> (OPF2) referenciado pelo
    // capítulo `index` — "cover", "title-page", "copyright-page", etc., ver
    // isNarrativeGuideType em EpubLoader.cpp. "" quando o capítulo é
    // narrativo ou o índice está fora do intervalo. O chamador decide o
    // rótulo em português (ver AppReader::updateIndexing / data/script.js) —
    // este método só expõe o valor cru do OPF.
    String getChapterGuideType(int index) const;

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
    // Um bool por capítulo (mesma ordem/tamanho da spine), preenchido por
    // parseGuide(); vazio quando o OPF não tem <guide> nenhum.
    std::vector<bool> nonNarrativeChapters;
    // Espelha nonNarrativeChapters: o tipo bruto do <guide> ("cover",
    // "title-page", ...) por capítulo, em vez de só o bool. String vazia
    // onde nonNarrativeChapters seria false (ou o vector está vazio).
    // Preenchido no mesmo passo de parseGuide(), sem custo de parsing extra.
    std::vector<String> chapterGuideType;

    // Allocate UNZIP in PSRAM to avoid memory issues with the 41KB internal buffer
    UNZIP* zip;

    // Helper to parse XML for specific attribute
    String extractAttribute(const String& xml, const String& tag, const String& attr);
    // Helper to get text content of tag
    String extractTagContent(const String& xml, const String& tag);
    // Helper to extract metadata from OPF
    String extractMetadata(const String& xml, const String& tag);
    // Lê o <guide> do OPF (EPUB2) e marca em nonNarrativeChapters os índices
    // da spine cujo href é referenciado com um tipo não-narrativo. Chamado
    // por parseOpf() depois de a spine estar completa (precisa dela para
    // resolver href -> índice).
    void parseGuide(const String& xml);

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
