// Host test for the SAX-like HTML tokenizer (M3) that replaced the
// indexOf-based state machine in EpubLoader::parseHtmlToRichContent.
// Build: g++ -std=c++17 -I ../../lib/Book32_Core -o test_html_tokenizer test_html_tokenizer.cpp &&
// ./test_html_tokenizer
#include "HtmlTokenizer.h"
#include <cassert>
#include <cstdio>
#include <string>

using std::string;

static string nameOf(const HtmlToken& t) {
    return string(t.name, t.nameLen);
}
static string textOf(const HtmlToken& t) {
    return string(t.text, t.textLen);
}
static string attrsOf(const HtmlToken& t) {
    return string(t.attrs, t.attrsLen);
}

int main() {
    // 1. Basic start/text/end sequence, including nested inline styling.
    {
        string html = "<p>Hello <b>world</b>!</p>";
        HtmlTokenizer tok(html.c_str(), html.size());

        HtmlToken t1 = tok.next();
        assert(t1.type == HtmlTokenType::StartTag && nameOf(t1) == "p" && !t1.selfClosing);

        HtmlToken t2 = tok.next();
        assert(t2.type == HtmlTokenType::Text && textOf(t2) == "Hello ");

        HtmlToken t3 = tok.next();
        assert(t3.type == HtmlTokenType::StartTag && nameOf(t3) == "b");

        HtmlToken t4 = tok.next();
        assert(t4.type == HtmlTokenType::Text && textOf(t4) == "world");

        HtmlToken t5 = tok.next();
        assert(t5.type == HtmlTokenType::EndTag && nameOf(t5) == "b");

        HtmlToken t6 = tok.next();
        assert(t6.type == HtmlTokenType::Text && textOf(t6) == "!");

        HtmlToken t7 = tok.next();
        assert(t7.type == HtmlTokenType::EndTag && nameOf(t7) == "p");

        assert(tok.next().type == HtmlTokenType::EndOfInput);
    }

    // 2. Self-closing / void elements: both "<br/>" (the normal XHTML
    //    spelling — the old parser's tag=="br" check never matched this
    //    because the trailing '/' stayed glued onto the extracted name) and
    //    bare "<br>" come back selfClosing regardless of the trailing slash.
    {
        string html = "<br/><br><img src=\"x.jpg\">";
        HtmlTokenizer tok(html.c_str(), html.size());

        HtmlToken br1 = tok.next();
        assert(br1.type == HtmlTokenType::StartTag && nameOf(br1) == "br" && br1.selfClosing);

        HtmlToken br2 = tok.next();
        assert(br2.type == HtmlTokenType::StartTag && nameOf(br2) == "br" && br2.selfClosing);

        HtmlToken img = tok.next();
        assert(img.type == HtmlTokenType::StartTag && nameOf(img) == "img" && img.selfClosing);
        assert(attrsOf(img) == " src=\"x.jpg\"");
    }

    // 3. Comments are skipped wholesale, even ones containing '>' or
    //    markup-looking draft text (D9: Sigil/hand-edited EPUB commonly has
    //    "<!-- <p>rascunho</p> -->"). Splits the surrounding text into two
    //    runs, same as the old parser did (it flushed on every '<').
    {
        string html = "a<!-- <p>rascunho</p> -->b";
        HtmlTokenizer tok(html.c_str(), html.size());
        HtmlToken t1 = tok.next();
        assert(t1.type == HtmlTokenType::Text && textOf(t1) == "a");
        HtmlToken t2 = tok.next();
        assert(t2.type == HtmlTokenType::Text && textOf(t2) == "b");
        assert(tok.next().type == HtmlTokenType::EndOfInput);
    }
    // Unterminated comment: skip to end of input rather than looping forever.
    {
        string html = "a<!-- never closed";
        HtmlTokenizer tok(html.c_str(), html.size());
        HtmlToken t1 = tok.next();
        assert(t1.type == HtmlTokenType::Text && textOf(t1) == "a");
        assert(tok.next().type == HtmlTokenType::EndOfInput);
    }

    // 4. CDATA sections are skipped the same way.
    {
        string html = "x<![CDATA[ <p> ]]>y";
        HtmlTokenizer tok(html.c_str(), html.size());
        HtmlToken t1 = tok.next();
        assert(t1.type == HtmlTokenType::Text && textOf(t1) == "x");
        HtmlToken t2 = tok.next();
        assert(t2.type == HtmlTokenType::Text && textOf(t2) == "y");
    }

    // 5. Unterminated tag ("<p class=\"x\"" with no '>'): stop parsing
    //    instead of reading past the end of the buffer.
    {
        string html = "before<p class=\"x\"";
        HtmlTokenizer tok(html.c_str(), html.size());
        HtmlToken t1 = tok.next();
        assert(t1.type == HtmlTokenType::Text && textOf(t1) == "before");
        assert(tok.next().type == HtmlTokenType::EndOfInput);
    }

    // 6. htmlFindAttribute: the D12 "media-type" trap — a search for "type"
    //    must not match inside "media-type", and must find the real
    //    attribute regardless of where it sits or which quote style it uses.
    {
        string attrs = " media-type=\"application/x\" type='text/css' class=\"chapter-title\"";
        size_t len = 0;
        const char* v = htmlFindAttribute(attrs.c_str(), attrs.size(), "type", &len);
        assert(v && string(v, len) == "text/css");

        const char* c = htmlFindAttribute(attrs.c_str(), attrs.size(), "class", &len);
        assert(c && string(c, len) == "chapter-title");

        assert(htmlFindAttribute(attrs.c_str(), attrs.size(), "missing", &len) == nullptr);
        assert(len == 0);
    }
    // Unterminated attribute value: no crash, no match.
    {
        string attrs = " style=\"text-align:center";
        size_t len = 99;
        assert(htmlFindAttribute(attrs.c_str(), attrs.size(), "style", &len) == nullptr);
        assert(len == 0);
    }

    // 7. HtmlElementStack: real name matching, not size snapshots.
    {
        HtmlElementStack st;
        st.push("p", 1);
        st.push("b", 1);
        // </p> with a still-open <b> inside it auto-closes the b too (D10:
        // the old blockStack-size-snapshot approach also did this for the
        // *style* stack, but only because it never actually checked tag
        // names — a real mismatch like </div> below now correctly fails
        // instead of silently popping the wrong frame).
        assert(st.pop("p", 1) == true);
        assert(st.empty());
    }
    {
        HtmlElementStack st;
        st.push("p", 1);
        // Stray close with no matching open: no-op, reports failure.
        assert(st.pop("div", 3) == false);
        assert(st.size() == 1);
        // The real one still matches afterwards.
        assert(st.pop("p", 1) == true);
        assert(st.empty());
    }
    {
        // Case-insensitive matching, and popping past nothing at all.
        HtmlElementStack st;
        assert(st.pop("P", 1) == false);
        st.push("P", 1);
        assert(st.pop("p", 1) == true);
    }

    printf("All HtmlTokenizer tests passed\n");
    return 0;
}
