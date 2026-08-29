// Host test for the pure "go to %" position math (v1.14.0).
// Build: g++ -std=c++17 -I ../../lib/Book32_Core -o test_go_to_percent test_go_to_percent.cpp &&
// ./test_go_to_percent
#include "GoToPercentLogic.h"
#include <cassert>
#include <cstdio>

int main() {
    // 1. Empty book: nothing to target.
    {
        std::vector<long> lengths;
        ChapterPercentTarget t = resolvePercentTarget(lengths, 50);
        assert(t.chapterIndex == -1);
    }

    // 2. 0% always lands at the very start of chapter 0.
    {
        std::vector<long> lengths = {1000, 2000, 3000};
        ChapterPercentTarget t = resolvePercentTarget(lengths, 0);
        assert(t.chapterIndex == 0 && t.charOffsetInChapter == 0);
    }

    // 3. 100% lands inside the last chapter, not past its end.
    {
        std::vector<long> lengths = {1000, 2000, 3000};
        ChapterPercentTarget t = resolvePercentTarget(lengths, 100);
        assert(t.chapterIndex == 2);
        assert(t.charOffsetInChapter >= 0 && t.charOffsetInChapter < 3000);
    }

    // 4. A single-chapter book: any percent stays in chapter 0.
    {
        std::vector<long> lengths = {5000};
        for (int p : {0, 25, 50, 75, 100}) {
            ChapterPercentTarget t = resolvePercentTarget(lengths, p);
            assert(t.chapterIndex == 0);
            assert(t.charOffsetInChapter >= 0 && t.charOffsetInChapter < 5000);
        }
    }

    // 5. Midpoint of an even three-way split lands in the middle chapter.
    {
        std::vector<long> lengths = {1000, 1000, 1000}; // total 3000
        ChapterPercentTarget t = resolvePercentTarget(lengths, 50);
        // Target offset = 1500 -> chapter 1 (covers [1000, 2000)), offset 500.
        assert(t.chapterIndex == 1);
        assert(t.charOffsetInChapter == 500);
    }

    // 6. A short chapter can be skipped over entirely by a percent step: the
    //    10-char chapter 1 only covers [9000, 9010), and 91% (offset 9100)
    //    lands past it, in chapter 2.
    {
        std::vector<long> lengths = {9000, 10, 990}; // total 10000
        ChapterPercentTarget t = resolvePercentTarget(lengths, 91);
        assert(t.chapterIndex == 2);
        assert(t.charOffsetInChapter == 90);
    }

    // 7. All-zero-length chapters (e.g. an all-image book) fall back to a
    //    proportional chapter index instead of refusing the jump.
    {
        std::vector<long> lengths = {0, 0, 0, 0};
        ChapterPercentTarget t = resolvePercentTarget(lengths, 75);
        assert(t.chapterIndex == 3);
        assert(t.charOffsetInChapter == 0);
    }

    // 8. Percent outside [0,100] is clamped, not undefined.
    {
        std::vector<long> lengths = {1000, 1000};
        ChapterPercentTarget under = resolvePercentTarget(lengths, -20);
        assert(under.chapterIndex == 0 && under.charOffsetInChapter == 0);
        ChapterPercentTarget over = resolvePercentTarget(lengths, 250);
        assert(over.chapterIndex == 1);
    }

    // 9. Node resolution: offset 0 (or an empty node list) stays at node 0.
    {
        std::vector<int> nodeLengths = {50, 30, 20};
        NodePositionTarget t = resolveNodeTarget(nodeLengths, 0);
        assert(t.nodeIndex == 0 && t.charOffsetInNode == 0);

        std::vector<int> empty;
        NodePositionTarget e = resolveNodeTarget(empty, 40);
        assert(e.nodeIndex == 0 && e.charOffsetInNode == 0);
    }

    // 10. Node resolution: an offset inside the second node resolves relative
    //     to that node's own start, not the chapter's.
    {
        std::vector<int> nodeLengths = {50, 30, 20}; // cumulative: 50, 80, 100
        NodePositionTarget t = resolveNodeTarget(nodeLengths, 65);
        assert(t.nodeIndex == 1 && t.charOffsetInNode == 15);
    }

    // 11. Node resolution: an offset past every node's length falls back to
    //     the start of the last node rather than an out-of-range index.
    {
        std::vector<int> nodeLengths = {50, 30, 20};
        NodePositionTarget t = resolveNodeTarget(nodeLengths, 999);
        assert(t.nodeIndex == 2 && t.charOffsetInNode == 0);
    }

    printf("All go-to-percent tests passed.\n");
    return 0;
}
