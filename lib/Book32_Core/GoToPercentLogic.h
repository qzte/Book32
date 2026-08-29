#pragma once
// Book32 v1.14.0 — pure "go to %" position math.
//
// No ArduinoJson, no EpubLoader, no Arduino String: only plain structs, ints
// and std::vector, so every decision here is host-testable
// (tools/tests/test_go_to_percent.cpp). Same pattern as BookOrderLogic.h and
// ProgressMergeLogic.h.
//
// The reader has no per-page index (see AppReader::_pageHistory — in-memory,
// current chapter only, cleared on every chapter change), so an exact "page N
// of totalPages" jump would mean re-paginating the whole book, which is the
// same cost the total-page counter already avoids doing synchronously (see
// AppReader::startTotalPagesCounting). Content-length proportion is the
// cheap alternative: walk each chapter's text length once (no font
// measurement, no rendering), then place the target percent inside whichever
// chapter holds that fraction of the book's total characters. It lands
// close, not exact — the same trade every e-reader with a "go to %" feature
// makes, since page counts are reflow-dependent anyway.

#include <cstddef>
#include <vector>

// Result of mapping a percent into the book: which chapter, and how far
// (in characters) into that chapter's text.
struct ChapterPercentTarget {
    int chapterIndex;         // -1 when chapterLengths is empty (nothing to target)
    long charOffsetInChapter; // 0-based, always < that chapter's length when it has any
};

// chapterLengths[i] = character count of chapter i (>= 0, one entry per
// chapter, in book order). percent is clamped to [0, 100].
inline ChapterPercentTarget resolvePercentTarget(const std::vector<long>& chapterLengths, int percent) {
    ChapterPercentTarget result{-1, 0};
    if (chapterLengths.empty()) return result;

    int clamped = percent;
    if (clamped < 0) clamped = 0;
    if (clamped > 100) clamped = 100;

    long total = 0;
    for (long len : chapterLengths)
        total += len;

    if (total <= 0) {
        // No measurable text anywhere (e.g. an all-image book) — fall back to
        // a proportional chapter index instead of refusing the jump outright.
        size_t idx = (size_t)(((long long)clamped * (long long)chapterLengths.size()) / 100);
        if (idx >= chapterLengths.size()) idx = chapterLengths.size() - 1;
        result.chapterIndex = (int)idx;
        result.charOffsetInChapter = 0;
        return result;
    }

    long targetOffset = (long)(((long long)total * (long long)clamped) / 100);
    if (targetOffset >= total) targetOffset = total - 1; // land inside the last chapter, not past it

    long cumulative = 0;
    for (size_t i = 0; i < chapterLengths.size(); ++i) {
        long len = chapterLengths[i];
        bool isLast = (i == chapterLengths.size() - 1);
        if (targetOffset < cumulative + len || isLast) {
            result.chapterIndex = (int)i;
            long offsetInChapter = targetOffset - cumulative;
            result.charOffsetInChapter = (offsetInChapter < 0) ? 0 : offsetInChapter;
            return result;
        }
        cumulative += len;
    }
    return result; // unreachable: the isLast branch above always returns
}

// Result of mapping a char offset (relative to the start of one chapter) to
// a specific content node within it.
struct NodePositionTarget {
    int nodeIndex;
    int charOffsetInNode;
};

// nodeLengths[i] = character count of content node i within one chapter, in
// the same order EpubLoader::getChapterContentRich() returns them.
// targetCharOffset is 0-based, from the chapter's start.
inline NodePositionTarget resolveNodeTarget(const std::vector<int>& nodeLengths, long targetCharOffset) {
    NodePositionTarget result{0, 0};
    if (nodeLengths.empty() || targetCharOffset <= 0) return result;

    long cumulative = 0;
    for (size_t i = 0; i < nodeLengths.size(); ++i) {
        int len = nodeLengths[i];
        if (targetCharOffset < cumulative + len) {
            result.nodeIndex = (int)i;
            result.charOffsetInNode = (int)(targetCharOffset - cumulative);
            return result;
        }
        cumulative += len;
    }
    // Past every node's text (offset landed in trailing whitespace/markup
    // that carries no length): start of the last node beats an out-of-range
    // index.
    result.nodeIndex = (int)nodeLengths.size() - 1;
    result.charOffsetInNode = 0;
    return result;
}
