// Host test for the pure "hyphenate at line wrap" decision extracted from
// TextRenderer::renderRichPageDynamic() (M2).
// Build: g++ -std=c++17 -I ../../lib/Book32_Core -o test_line_breaker test_line_breaker.cpp &&
// ./test_line_breaker
#include "LineBreaker.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using std::string;

static unsigned char widths[256];

static int widthOf(const string& s) {
    int w = 0;
    for (unsigned char c : s)
        w += widths[c];
    return w;
}

int main() {
    memset(widths, 0, sizeof(widths));
    for (int c = 0x20; c < 0x100; c++)
        widths[c] = 10;

    // 1. justHyphenated suppresses a break even when a valid one exists and
    //    would otherwise be taken (case 2 below, same word/budget).
    {
        string w = "computador"; // com-pu-ta-dor: hyphenationPoints -> {3,5,7}
        WordFit f = tryHyphenateAtWrap(w.c_str(), w.size(), widthOf(w), 100, 45, widths,
                                       /*justHyphenated=*/true);
        assert(!f.hyphen && f.take == 0 && f.width == 0);
    }

    // 2. Enough budget for the earliest break point (3): matches what
    //    fitWordIntoLineHyphenated gives directly with that point list
    //    (test_word_fit.cpp case 11 uses the same numbers).
    {
        string w = "computador";
        WordFit f = tryHyphenateAtWrap(w.c_str(), w.size(), widthOf(w), 100, 45, widths, false);
        assert(f.hyphen && f.take == 3 && f.width == 40);
    }

    // 3. Budget too small for any break point (3,5,7 all need more pixels
    //    than the 25px given): fitWordIntoLineHyphenated falls back to the
    //    plain character cut internally, so the result has hyphen=false but
    //    still matches that plain fit exactly. The caller only acts on this
    //    when hyphen==true, so this is equivalent to "no break" for it.
    {
        string w = "computador";
        WordFit f = tryHyphenateAtWrap(w.c_str(), w.size(), widthOf(w), 100, 25, widths, false);
        WordFit plain = fitWordIntoLine(w.c_str(), w.size(), widthOf(w), 100, 25, widths);
        assert(!f.hyphen);
        assert(f.take == plain.take && f.width == plain.width);
    }

    // 4. bufLeft <= 0 or pixelBudget <= 0: no break, regardless of the word.
    {
        string w = "computador";
        assert(!tryHyphenateAtWrap(w.c_str(), w.size(), widthOf(w), 0, 45, widths, false).hyphen);
        assert(!tryHyphenateAtWrap(w.c_str(), w.size(), widthOf(w), 100, 0, widths, false).hyphen);
        assert(!tryHyphenateAtWrap(w.c_str(), w.size(), widthOf(w), -5, 45, widths, false).hyphen);
    }

    // 5. The >=3-letters-each-side rule is strictly tighter than the >=2
    //    hyphenationPoints() itself guarantees (HyphenationLogic.h test #11):
    //    p >= 3 && (len - p) >= 3 implies len >= 6, so no word shorter than
    //    6 letters can ever produce a break here, however plausible its
    //    linguistic syllable boundary. Ample budget on both sides confirms
    //    the filter — not the budget — is what rules these out.
    {
        for (const char* word : {"casa", "gatos", "email"}) {
            string w = word;
            WordFit f = tryHyphenateAtWrap(w.c_str(), w.size(), widthOf(w), 100, 1000, widths, false);
            assert(!f.hyphen);
        }
    }

    // 6. Degenerate input: empty word never breaks.
    { assert(!tryHyphenateAtWrap("", 0, 0, 100, 100, widths, false).hyphen); }

    printf("All line breaker tests passed.\n");
    return 0;
}
