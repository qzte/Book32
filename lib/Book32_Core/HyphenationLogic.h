#pragma once
// Book32 v1.15.0 — pure Portuguese syllable-hyphenation logic.
//
// No ArduinoJson, no EpubLoader, no Arduino String: only plain bytes and
// std::vector, so every decision here is host-testable
// (tools/tests/test_hyphenation.cpp). Same pattern as WordFitLogic.h and
// GoToPercentLogic.h.
//
// Rationale (gap #3 of docs/plans/2026-08-29-avaliacao-crosspoint-reader.md):
// today a Portuguese word too wide to fit its own line gets cut at whatever
// character happens to reach the pixel budget (see the fallback branch in
// WordFitLogic.h's fitWordIntoLine), with no hyphen shown — jarring on a
// narrow 480px column, and Portuguese has plenty of long compound words that
// hit this. TextRenderer now tries a syllable-aware break first (see
// fitWordIntoLineHyphenated in WordFitLogic.h) and only falls back to the raw
// character cut when this file finds no legal break point.
//
// This is deliberately a small set of hand-written rules, not a Liang/TeX
// pattern-matching dictionary (the kind CrossPoint Reader's roadmap describes
// for its multi-language hyphenation — see the evaluation doc): a pattern
// file covering Portuguese well runs tens of KB, and Book32 only ever reads
// one language. The rules below are the ones taught as "separação silábica"
// in Portuguese schools:
//
//   1. A single consonant between two vowels joins the FOLLOWING vowel:
//      "ca-sa", not "cas-a".
//   2. "ch", "lh", "nh" (each one sound) and the standard consonant+liquid
//      blends (bl, br, cl, cr, dr, fl, fr, gl, gr, pl, pr, tr, vr, ...) never
//      split — both letters join the following vowel: "ma-lha", "li-vro".
//   3. Any other run of 2+ consonants splits after all but the last one:
//      "car-ro", "com-pu-tar" (only the last consonant of a cluster ever
//      joins the next vowel unless rule 2 applies to the last two).
//   4. Adjacent vowels stay in one syllable (diphthong) UNLESS the second one
//      carries an acute accent on í or ú — the actual Portuguese spelling
//      convention for marking a forced hiatus: "sa-ú-de", "pa-ís".
//   5. "u" is always classed as a vowel here, never as a consonant. That is
//      what keeps "qu"/"gu" attached to the vowel that follows without a
//      special case for them: in "quan-do" and "lin-gua-gem" the "u" simply
//      merges into the next syllable's vowel cluster like any diphthong.
//
// This does not aim to enumerate every syllable boundary a linguist would —
// only enough of them, correctly, to give the line-wrapper a legal place to
// break a long word. A word containing anything other than plain letters
// (digits, punctuation, an existing hyphen) is left alone entirely rather
// than guessed at.

#include <vector>

namespace book32_hyphenation_detail {

// Latin-1 bytes for the accented vowels Book32's fonts render (see
// FontMgr::utf8ToLatin1 and the 0x20-0xFF glyph range shared by every
// reading font).
inline bool isVowel(unsigned char c) {
    switch (c) {
        case 'a':
        case 'A':
        case 'e':
        case 'E':
        case 'i':
        case 'I':
        case 'o':
        case 'O':
        case 'u':
        case 'U':
        case 0xE0:
        case 0xC0: // à À
        case 0xE1:
        case 0xC1: // á Á
        case 0xE2:
        case 0xC2: // â Â
        case 0xE3:
        case 0xC3: // ã Ã
        case 0xE8:
        case 0xC8: // è È
        case 0xE9:
        case 0xC9: // é É
        case 0xEA:
        case 0xCA: // ê Ê
        case 0xEC:
        case 0xCC: // ì Ì
        case 0xED:
        case 0xCD: // í Í
        case 0xF2:
        case 0xD2: // ò Ò
        case 0xF3:
        case 0xD3: // ó Ó
        case 0xF4:
        case 0xD4: // ô Ô
        case 0xF5:
        case 0xD5: // õ Õ
        case 0xF9:
        case 0xD9: // ù Ù
        case 0xFA:
        case 0xDA: // ú Ú
        case 0xFC:
        case 0xDC: // ü Ü
            return true;
        default:
            return false;
    }
}

// The acute accent on í/ú is Portuguese orthography's own marker for a
// forced hiatus (two vowels pronounced, and therefore syllabified,
// separately) — see rule 4 above.
inline bool isHiatusMarker(unsigned char c) {
    switch (c) {
        case 0xED:
        case 0xCD: // í Í
        case 0xFA:
        case 0xDA: // ú Ú
            return true;
        default:
            return false;
    }
}

inline bool isLetter(unsigned char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return true;
    if (c == 0xE7 || c == 0xC7) return true; // ç Ç
    return isVowel(c);
}

inline unsigned char toLowerAscii(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? (unsigned char)(c + 32) : c;
}

// c1c2 is the last two letters of a consonant cluster, in word order. True
// when they must stay together and join the following vowel (rule 2).
inline bool isInseparablePair(unsigned char c1, unsigned char c2) {
    unsigned char a = toLowerAscii(c1);
    unsigned char b = toLowerAscii(c2);
    if ((a == 'c' && b == 'h') || (a == 'l' && b == 'h') || (a == 'n' && b == 'h')) return true;
    if (b == 'l' || b == 'r') {
        switch (a) {
            case 'b':
            case 'c':
            case 'd':
            case 'f':
            case 'g':
            case 'p':
            case 't':
            case 'v':
                return true;
            default:
                return false;
        }
    }
    return false;
}

} // namespace book32_hyphenation_detail

// Returns 0-based byte offsets into `word` marking legal hyphenation points:
// a hyphen may be drawn between word[p-1] and word[p]. Points closer than 2
// letters to either end are dropped (the standard minimum: at least two
// letters on each side of the hyphen). Empty for anything shorter than 5
// letters, or containing so much as one non-letter byte — safer to leave a
// number, URL, or already-punctuated token alone than guess.
inline std::vector<int> hyphenationPoints(const char* word, int len) {
    using namespace book32_hyphenation_detail;
    std::vector<int> points;
    if (!word || len < 5) return points;

    for (int i = 0; i < len; i++) {
        if (!isLetter((unsigned char)word[i])) return points;
    }

    int i = 0;
    int prevVowelEnd = -1; // end (exclusive) of the most recently closed vowel cluster
    while (i < len) {
        if (!isVowel((unsigned char)word[i])) {
            i++;
            continue;
        }

        // Start of a vowel cluster (nucleus): absorb following vowels that
        // form a diphthong, stopping at a hiatus marker instead (rule 4).
        int vowelStart = i;
        i++;
        while (i < len && isVowel((unsigned char)word[i])) {
            if (isHiatusMarker((unsigned char)word[i])) {
                points.push_back(i); // break right between the two vowels
                break;
            }
            i++;
        }
        int vowelEnd = i;

        if (prevVowelEnd >= 0) {
            int consonants = vowelStart - prevVowelEnd;
            if (consonants >= 1) {
                int withNext = 1;
                if (consonants >= 2 && isInseparablePair((unsigned char)word[vowelStart - 2],
                                                         (unsigned char)word[vowelStart - 1])) {
                    withNext = 2;
                }
                points.push_back(vowelStart - withNext);
            }
        }
        prevVowelEnd = vowelEnd;
    }

    std::vector<int> filtered;
    filtered.reserve(points.size());
    for (int p : points) {
        if (p >= 2 && (len - p) >= 2) filtered.push_back(p);
    }
    return filtered;
}
