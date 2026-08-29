// Host test for the pure Portuguese hyphenation logic (v1.15.0).
// Build: g++ -std=c++17 -I ../../lib/Book32_Core -o test_hyphenation test_hyphenation.cpp &&
// ./test_hyphenation
#include "HyphenationLogic.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static bool has(const std::vector<int>& points, int p) {
    for (int x : points)
        if (x == p) return true;
    return false;
}

int main() {
    // 1. Too short to bother with (under 5 letters): no points at all, even
    //    though "casa" does have a linguistically valid ca-sa boundary.
    {
        auto p = hyphenationPoints("casa", 4);
        assert(p.empty());
    }

    // 2. Single consonant between two vowels joins the following vowel.
    {
        auto p = hyphenationPoints("gatos", 5); // ga-tos
        assert(p.size() == 1 && has(p, 2));
    }

    // 3. Doubled consonants (rr, ss) split BETWEEN the pair, unlike ch/lh/nh.
    {
        auto p = hyphenationPoints("carros", 6); // car-ros
        assert(has(p, 3));
    }
    {
        auto p = hyphenationPoints("passeio", 7); // pas-sei-o (passeio has a diphthong ei too)
        assert(has(p, 3));
    }

    // 4. ch/lh/nh digraphs never split — both letters join the next vowel.
    {
        auto p = hyphenationPoints("malhado", 7); // ma-lha-do
        assert(has(p, 2));
        assert(!has(p, 3)); // never split "l" from "h"
    }
    {
        auto p = hyphenationPoints("banhista", 8); // ba-nhis-ta
        assert(has(p, 2));
        assert(!has(p, 3));
    }
    {
        auto p = hyphenationPoints("fechado", 7); // fe-cha-do
        assert(has(p, 2));
        assert(!has(p, 3));
    }

    // 5. Consonant+liquid blends (br, cl, fr, tr, vr...) never split either.
    {
        auto p = hyphenationPoints("livraria", 8); // li-vra-ri-a
        assert(has(p, 2));
        assert(!has(p, 3));
    }
    {
        auto p = hyphenationPoints("abrigado", 8); // a-bri-ga-do; "a-" itself
        // is filtered (only 1 letter before it), but "abri-gado" survives.
        assert(!has(p, 1));
        assert(has(p, 4));
    }

    // 6. A non-blend, non-digraph two-consonant cluster splits between them.
    {
        auto p = hyphenationPoints("computador", 10); // com-pu-ta-dor
        assert(has(p, 3));                            // com | putador
        assert(has(p, 5));                            // compu | tador
        assert(has(p, 7));                            // computa | dor
    }

    // 7. qu/gu: the "u" is classed as a vowel, so it rides along with the
    //    vowel that follows instead of splitting from it.
    {
        auto p = hyphenationPoints("quando", 6); // quan-do
        assert(has(p, 4));
        assert(!has(p, 1)); // never split "q" from "u"
    }
    {
        auto p = hyphenationPoints("linguagem", 9); // lin-gua-gem
        assert(has(p, 3));
        assert(has(p, 6));
        assert(!has(p, 5)); // never split "g" from "u"
    }

    // 8. Diphthongs (adjacent plain vowels) never split.
    {
        auto p = hyphenationPoints("cadeira", 7); // ca-dei-ra: "ei" stays together
        assert(!has(p, 4));                       // no break between e and i
    }

    // 9. A forced hiatus (accented í/ú after another vowel) DOES split,
    //    right between the two vowels — this is what marks "saída"/"saúde"
    //    as pronounced (and hyphenated) in more syllables than a plain
    //    diphthong would be. ("país" itself is only 4 letters, below the
    //    minimum-length gate — case 1 above covers that gate separately.)
    {
        auto p = hyphenationPoints("sa\xED"
                                   "da",
                                   5); // "saída", í = 0xED
        assert(has(p, 2));
    }
    {
        auto p = hyphenationPoints("sa\xFA"
                                   "de",
                                   5); // "saúde", ú = 0xFA
        assert(has(p, 2));
    }

    // 10. Anything with a non-letter byte (digits, an existing hyphen,
    //     punctuation) is left alone entirely.
    {
        auto p = hyphenationPoints("abcde123", 8);
        assert(p.empty());
    }
    {
        auto p = hyphenationPoints("guarda-chuva", 12);
        assert(p.empty());
    }

    // 11. No break point ever lands closer than 2 letters to either end.
    {
        auto p = hyphenationPoints("email", 5); // e-mail: "e" alone is too short
        for (int x : p)
            assert(x >= 2 && (5 - x) >= 2);
    }

    // 12. Empty/degenerate input.
    {
        assert(hyphenationPoints(nullptr, 5).empty());
        assert(hyphenationPoints("", 0).empty());
    }

    printf("All hyphenation tests passed.\n");
    return 0;
}
