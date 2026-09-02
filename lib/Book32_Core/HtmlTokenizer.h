#pragma once
// Book32 — SAX-like HTML/XHTML tokenizer over raw bytes (M3,
// docs/plans/2026-09-02-avaliacao-codigo-ereader.md).
//
// Rationale: EpubLoader::parseHtmlToRichContent used to scan tags with
// String::indexOf and track "open elements" as size-snapshots of a shared
// style stack (styleStack.size() pushed onto a blockStack at every block
// open) instead of a real stack of element names. A stray, mismatched, or
// unclosed close tag therefore popped whatever frame happened to be on top
// instead of being ignored (no matching open) or resolving the actual
// nesting — D2, D9 and D10 in the evaluation above were all symptoms of that
// one design gap. This tokenizer, plus HtmlElementStack below, give the
// parser real events (startTag/endTag/text) and a real per-tag-name stack.
// Comments and CDATA sections are skipped wholesale as single tokens' worth
// of scanning; no DOM is built, matching what a partial-CSS pass (the next
// consumer of this) will need anyway.
//
// Pure over const char*/size_t: no Arduino String, no heap allocation beyond
// the element-name stack itself (std::string/std::vector, already used
// elsewhere in this directory — see SafeName.h, BookTitleLogic.h). Host-
// testable without the Arduino.h stub the rest of EpubLoader.cpp still needs:
// tools/tests/test_html_tokenizer.cpp.

#include <cctype>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

enum class HtmlTokenType {
    StartTag,
    EndTag,
    Text,
    EndOfInput,
};

struct HtmlToken {
    HtmlTokenType type = HtmlTokenType::EndOfInput;

    // StartTag / EndTag: raw tag name as found in the markup (mixed case
    // preserved — callers compare case-insensitively, see htmlTagEquals()).
    const char* name = nullptr;
    size_t nameLen = 0;

    // StartTag only: true for "<tag/>" or a well-known void element (br,
    // img, meta, ...) even without the trailing slash — real EPUB XHTML uses
    // both spellings, and treating only the former as self-closing was a
    // latent bug (a self-closed "<br/>", the normal XHTML spelling, never
    // matched the old parser's "<br>"-shaped tag check).
    bool selfClosing = false;

    // StartTag only: raw span between the tag name and the closing '>' (or
    // "/>"), e.g. ` class="x" style="text-align:center"`. Not parsed here —
    // see htmlFindAttribute() below for pulling a single value out of it.
    const char* attrs = nullptr;
    size_t attrsLen = 0;

    // Text only: a run of bytes with no '<' in it. Entity decoding and
    // whitespace collapsing stay the caller's job (EpubLoader.cpp), same as
    // before.
    const char* text = nullptr;
    size_t textLen = 0;
};

// HTML "void" elements: never have a matching close tag or children, so the
// tokenizer treats them as self-closing regardless of a trailing '/'. Covers
// the full HTML5 void-element list plus "image" (SVG's void element, used by
// EPUB covers embedded as inline SVG) for the same reason the previous
// parser special-cased "img"/"image" together.
inline bool htmlIsVoidElement(const char* name, size_t len) {
    static const char* const kVoid[] = {
        "area",  "base", "br",   "col",   "embed",  "hr",    "img", "image",
        "input", "link", "meta", "param", "source", "track", "wbr",
    };
    for (const char* v : kVoid) {
        size_t vlen = strlen(v);
        if (vlen != len) continue;
        bool match = true;
        for (size_t i = 0; i < len; i++) {
            if (tolower((unsigned char)name[i]) != v[i]) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

// Case-insensitive "is this token's tag this literal name" check.
inline bool htmlTagEquals(const HtmlToken& t, const char* name) {
    size_t len = strlen(name);
    if (t.nameLen != len) return false;
    for (size_t i = 0; i < len; i++) {
        if (tolower((unsigned char)t.name[i]) != tolower((unsigned char)name[i])) return false;
    }
    return true;
}

// True when the tag's (lowercased) name starts with `c` — used for the
// "<h1>...<h6>, plus anything else starting with h (<header>, ...)" grouping
// the old parser applied to block-level elements it didn't otherwise
// recognize by exact name.
inline bool htmlTagFirstCharIs(const HtmlToken& t, char c) {
    return t.nameLen > 0 && tolower((unsigned char)t.name[0]) == c;
}

// Finds attr="value" or attr='value' within a StartTag token's raw `attrs`
// span. Returns a pointer into that span and sets *outLen; nullptr (and
// *outLen = 0) when not found. The match must be preceded by whitespace (or
// start the span) — same fix as findAttributeStart() in EpubLoader.cpp: a
// search for "type" must not match inside "media-type".
inline const char* htmlFindAttribute(const char* attrs, size_t attrsLen, const char* attrName,
                                     size_t* outLen) {
    if (outLen) *outLen = 0;
    if (!attrs || !attrName) return nullptr;
    size_t nameLen = strlen(attrName);
    size_t i = 0;
    while (i + nameLen + 2 <= attrsLen) {
        bool nameMatches = strncmp(attrs + i, attrName, nameLen) == 0;
        char afterName = nameMatches ? attrs[i + nameLen] : '\0';
        char quote = nameMatches ? attrs[i + nameLen + 1] : '\0';
        bool precededOk = (i == 0) || isspace((unsigned char)attrs[i - 1]);
        if (nameMatches && afterName == '=' && (quote == '"' || quote == '\'') && precededOk) {
            size_t valStart = i + nameLen + 2;
            size_t valEnd = valStart;
            while (valEnd < attrsLen && attrs[valEnd] != quote)
                valEnd++;
            if (valEnd >= attrsLen) return nullptr; // unterminated value
            if (outLen) *outLen = valEnd - valStart;
            return attrs + valStart;
        }
        i++;
    }
    return nullptr;
}

// Scans one HTML/XHTML document, one token at a time. Comments (<!-- ... -->)
// and CDATA sections (<![CDATA[ ... ]]>) are consumed internally and never
// surface as tokens — a comment containing '>' or markup-looking text (both
// common in hand-edited or Sigil-produced EPUB) no longer leaks into the
// output. Anything else starting with "<!" or "<?" (doctype, processing
// instructions) comes back as an ordinary, unrecognized StartTag that every
// caller here already ignores, same as the previous parser's behaviour.
class HtmlTokenizer {
  public:
    HtmlTokenizer(const char* html, size_t len) : html_(html), len_(len), pos_(0) {}

    // Current byte offset into the input — callers that special-case a tag
    // (e.g. <table>, whose whole subtree the caller wants to re-scan and
    // skip as one block) read this right after receiving that tag's
    // StartTag token and pass it to seekTo() once they're done.
    size_t position() const {
        return pos_;
    }
    void seekTo(size_t pos) {
        pos_ = (pos <= len_) ? pos : len_;
    }

    HtmlToken next() {
        for (;;) {
            if (pos_ >= len_) return HtmlToken{};
            if (html_[pos_] != '<') return readText();

            if (matchesAt("<!--")) {
                size_t end = findFrom("-->", pos_ + 4);
                pos_ = (end == npos()) ? len_ : end + 3;
                continue;
            }
            if (matchesAt("<![CDATA[")) {
                size_t end = findFrom("]]>", pos_ + 9);
                pos_ = (end == npos()) ? len_ : end + 3;
                continue;
            }

            bool closing = pos_ + 1 < len_ && html_[pos_ + 1] == '/';
            return closing ? readEndTag() : readStartTag();
        }
    }

  private:
    static size_t npos() {
        return (size_t)-1;
    }

    bool matchesAt(const char* needle) const {
        size_t n = strlen(needle);
        if (pos_ + n > len_) return false;
        return strncmp(html_ + pos_, needle, n) == 0;
    }

    size_t findFrom(const char* needle, size_t from) const {
        size_t n = strlen(needle);
        if (n == 0 || from > len_) return npos();
        for (size_t i = from; i + n <= len_; i++) {
            if (strncmp(html_ + i, needle, n) == 0) return i;
        }
        return npos();
    }

    static bool isNameEnd(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '/' || c == '>';
    }

    HtmlToken readText() {
        size_t start = pos_;
        while (pos_ < len_ && html_[pos_] != '<')
            pos_++;
        HtmlToken t;
        t.type = HtmlTokenType::Text;
        t.text = html_ + start;
        t.textLen = pos_ - start;
        return t;
    }

    HtmlToken readEndTag() {
        size_t gt = findFrom(">", pos_);
        if (gt == npos()) {
            pos_ = len_;
            return HtmlToken{};
        }
        size_t nameStart = pos_ + 2; // skip "</"
        size_t nameEnd = nameStart;
        while (nameEnd < gt && !isNameEnd(html_[nameEnd]))
            nameEnd++;
        HtmlToken t;
        t.type = HtmlTokenType::EndTag;
        t.name = html_ + nameStart;
        t.nameLen = (nameEnd > nameStart) ? nameEnd - nameStart : 0;
        pos_ = gt + 1;
        return t;
    }

    HtmlToken readStartTag() {
        size_t gt = findFrom(">", pos_);
        if (gt == npos()) {
            pos_ = len_;
            return HtmlToken{};
        }
        size_t nameStart = pos_ + 1; // skip "<"
        size_t nameEnd = nameStart;
        while (nameEnd < gt && !isNameEnd(html_[nameEnd]))
            nameEnd++;

        size_t attrsEnd = gt;
        bool slashClosed = gt > nameEnd && html_[gt - 1] == '/';
        if (slashClosed) attrsEnd = gt - 1;

        HtmlToken t;
        t.type = HtmlTokenType::StartTag;
        t.name = html_ + nameStart;
        t.nameLen = (nameEnd > nameStart) ? nameEnd - nameStart : 0;
        t.attrs = html_ + nameEnd;
        t.attrsLen = (attrsEnd > nameEnd) ? attrsEnd - nameEnd : 0;
        t.selfClosing = slashClosed || htmlIsVoidElement(t.name, t.nameLen);
        pos_ = gt + 1;
        return t;
    }

    const char* html_;
    size_t len_;
    size_t pos_;
};

// A stack of open element names, matched case-insensitively by exact name —
// what parseHtmlToRichContent used to fake with styleStack.size() snapshots.
class HtmlElementStack {
  public:
    void push(const char* name, size_t len) {
        names_.push_back(lower(name, len));
    }

    // Pops down to and including the innermost element named `name`
    // (auto-closing anything left open above it, same tolerant behaviour
    // real HTML parsers use for unclosed tags). Returns false and leaves the
    // stack untouched when no open element has that name — a stray close tag
    // with nothing to close, which used to corrupt the innermost frame
    // instead of being ignored.
    bool pop(const char* name, size_t len) {
        std::string target = lower(name, len);
        for (size_t i = names_.size(); i-- > 0;) {
            if (names_[i] == target) {
                names_.resize(i);
                return true;
            }
        }
        return false;
    }

    size_t size() const {
        return names_.size();
    }
    bool empty() const {
        return names_.empty();
    }

  private:
    static std::string lower(const char* s, size_t len) {
        std::string r(s, len);
        for (char& c : r)
            c = (char)tolower((unsigned char)c);
        return r;
    }

    std::vector<std::string> names_;
};
