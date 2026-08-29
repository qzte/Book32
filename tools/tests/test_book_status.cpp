// Host test for the pure reading-status derivation.
// Build: g++ -std=c++17 -I ../../lib/Book32_Core -o test_book_status test_book_status.cpp &&
// ./test_book_status
#include "BookStatusLogic.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    // --- progressPercent -----------------------------------------------
    // 1. Unknown total is unknown, not zero: "0%" would read as "not started"
    //    for a book that may be half read.
    assert(progressPercent(50, 0) == -1);
    assert(progressPercent(50, -1) == -1);

    // 2. globalPage is 1-based, so the first and last page are 1% and 100%.
    assert(progressPercent(1, 100) == 1);
    assert(progressPercent(100, 100) == 100);
    assert(progressPercent(50, 100) == 50);

    // 3. Out-of-range pages clamp instead of producing nonsense. A page count
    //    cached at one font size can be stale against a position saved at
    //    another, so globalPage > totalPages really does happen.
    assert(progressPercent(0, 100) == 0);
    assert(progressPercent(-5, 100) == 0);
    assert(progressPercent(300, 100) == 100);

    // 4. A large book must not overflow the multiply by 100.
    assert(progressPercent(30000000, 60000000) == 50);

    // --- deriveStatus: no entry ----------------------------------------
    // 5. Never opened: unread, and no percentage to show.
    {
        BookStatusView v = deriveStatus(false, StatusOverride::Auto, 0, 0);
        assert(v.status == BookStatus::Unread);
        assert(v.percent == -1);
    }
    // 6. An override still applies to a book with no reading position — this
    //    is the "read it elsewhere" case, and the whole reason the override
    //    exists.
    {
        BookStatusView v = deriveStatus(false, StatusOverride::Read, 0, 0);
        assert(v.status == BookStatus::Read);
        assert(v.percent == -1);
    }

    // --- deriveStatus: threshold boundaries ----------------------------
    // 7. Just below the threshold is still reading.
    {
        BookStatusView v = deriveStatus(true, StatusOverride::Auto, 95, 100);
        assert(v.status == BookStatus::Reading);
        assert(v.percent == 95);
    }
    // 8. Exactly at the threshold is read — the boundary is inclusive.
    {
        BookStatusView v = deriveStatus(true, StatusOverride::Auto, 96, 100);
        assert(v.status == BookStatus::Read);
        assert(v.percent == 96);
    }
    // 9. The last page is read.
    {
        BookStatusView v = deriveStatus(true, StatusOverride::Auto, 100, 100);
        assert(v.status == BookStatus::Read);
        assert(v.percent == 100);
    }
    // 10. Page 1 of a book being read is reading, not unread: having an entry
    //     at all means it was opened.
    {
        BookStatusView v = deriveStatus(true, StatusOverride::Auto, 1, 100);
        assert(v.status == BookStatus::Reading);
        assert(v.percent == 1);
    }

    // --- deriveStatus: total not counted yet ---------------------------
    // 11. An entry with no page count is reading with an unknown percent. It
    //     must never fall through to read: PageCountStore returns 0 both
    //     before the count finishes and right after a font change, and calling
    //     a book finished on the strength of that would be wrong.
    {
        BookStatusView v = deriveStatus(true, StatusOverride::Auto, 4000, 0);
        assert(v.status == BookStatus::Reading);
        assert(v.percent == -1);
    }

    // --- deriveStatus: overrides beat the derivation --------------------
    // 12. Marked unread while sitting at the end — the re-read case. It must
    //     not snap straight back to read.
    {
        BookStatusView v = deriveStatus(true, StatusOverride::Unread, 100, 100);
        assert(v.status == BookStatus::Unread);
        // The percentage is still reported: the position is real, and the UI
        // shows it even while the status says otherwise.
        assert(v.percent == 100);
    }
    // 13. Marked reading while past the threshold — for a book you have not
    //     actually finished despite the position.
    {
        BookStatusView v = deriveStatus(true, StatusOverride::Reading, 99, 100);
        assert(v.status == BookStatus::Reading);
        assert(v.percent == 99);
    }
    // 14. Marked read at the very start.
    {
        BookStatusView v = deriveStatus(true, StatusOverride::Read, 1, 100);
        assert(v.status == BookStatus::Read);
        assert(v.percent == 1);
    }

    // --- wire names -----------------------------------------------------
    // 15. Round trip: every override name parses back to itself.
    {
        const StatusOverride all[] = {StatusOverride::Auto, StatusOverride::Unread, StatusOverride::Reading,
                                      StatusOverride::Read};
        for (StatusOverride o : all) {
            StatusOverride parsed;
            assert(parseOverride(overrideKey(o), parsed));
            assert(parsed == o);
        }
    }
    // 16. Unrecognised input is rejected rather than silently becoming Auto,
    //     so the endpoint can answer 400 instead of wiping a real mark.
    {
        StatusOverride parsed = StatusOverride::Read;
        assert(!parseOverride("finished", parsed));
        assert(!parseOverride("", parsed));
        assert(!parseOverride(nullptr, parsed));
        assert(!parseOverride("READ", parsed));
        // A prefix of a valid name must not match.
        assert(!parseOverride("rea", parsed));
        // Nor must a valid name with trailing junk.
        assert(!parseOverride("readx", parsed));
        // Rejection leaves the out parameter untouched.
        assert(parsed == StatusOverride::Read);
    }
    // 17. Status names are the ones the web UI switches on.
    {
        assert(strcmp(statusKey(BookStatus::Unread), "unread") == 0);
        assert(strcmp(statusKey(BookStatus::Reading), "reading") == 0);
        assert(strcmp(statusKey(BookStatus::Read), "read") == 0);
    }

    // --- pruning exemption ----------------------------------------------
    // 18. Only an explicit "read" is a record worth keeping past the file.
    assert(isReadRecord(StatusOverride::Read));
    assert(!isReadRecord(StatusOverride::Auto));
    assert(!isReadRecord(StatusOverride::Unread));
    assert(!isReadRecord(StatusOverride::Reading));

    printf("test_book_status: all assertions passed\n");
    return 0;
}
