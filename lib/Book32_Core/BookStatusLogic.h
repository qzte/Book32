#pragma once
// Book32 — pure reading-status derivation.
//
// No ArduinoJson, no LittleFS, no Arduino String: only plain enums, ints and
// free functions, so every decision that can be wrong is host-testable
// (tools/tests/test_book_status.cpp). Same pattern as ProgressMergeLogic.h,
// GoToPercentLogic.h and BookOrderLogic.h.
//
// Deliberately depends on nothing else in Core — not even ProgressMergeLogic.h,
// which includes *this* for StatusOverride. The derivation takes primitives
// rather than a BookProgress so the dependency only ever points one way.

#include <cstdint>

// A book is called read once it passes this much of its page count.
//
// Not 100: a reader rarely lands on the very last page. Endnotes, appendices,
// bibliographies and the colophon mean a book that is finished for all
// practical purposes commonly stops in the mid-90s, and would otherwise never
// mark itself read no matter how long you left it.
static const int READ_THRESHOLD_PERCENT = 96;

// What the user said the status is, overriding the derivation. Only this is
// ever persisted — never the resulting status, which is recomputed on every
// read so that it keeps tracking the position as the book is read.
enum class StatusOverride : uint8_t {
    Auto = 0, // derive from the reading position
    Unread = 1,
    Reading = 2,
    Read = 3
};

enum class BookStatus : uint8_t { Unread, Reading, Read };

struct BookStatusView {
    BookStatus status = BookStatus::Unread;
    // 0..100, or -1 when the page count is not known yet. PageCountStore only
    // knows a total once the book has been counted through at the current font
    // settings, so "reading, percent unknown" is a normal, temporary state and
    // not an error.
    int percent = -1;
};

// Percent of the way through a book. `globalPage` is 1-based, so page 1 of 100
// is 1% and page 100 of 100 is 100%. Returns -1 when there is no total to
// measure against.
inline int progressPercent(int globalPage, int totalPages) {
    if (totalPages <= 0) return -1;
    if (globalPage <= 0) return 0;
    long percent = ((long)globalPage * 100) / (long)totalPages;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    return (int)percent;
}

// The status to show for one book.
//
// `hasEntry` is whether ProgressStore holds anything for it: a book that was
// never opened has no entry, which is exactly what unread means. An explicit
// override always wins — including over the threshold, so a book you mark
// unread to read again does not immediately snap back to read.
inline BookStatusView deriveStatus(bool hasEntry, StatusOverride override, int globalPage, int totalPages) {
    BookStatusView view;
    view.percent = hasEntry ? progressPercent(globalPage, totalPages) : -1;

    switch (override) {
        case StatusOverride::Unread:
            view.status = BookStatus::Unread;
            return view;
        case StatusOverride::Reading:
            view.status = BookStatus::Reading;
            return view;
        case StatusOverride::Read:
            view.status = BookStatus::Read;
            return view;
        case StatusOverride::Auto:
            break;
    }

    if (!hasEntry) {
        view.status = BookStatus::Unread;
        return view;
    }
    // Percent unknown means the book has not finished counting; it cannot be
    // called read yet, but it is certainly being read.
    view.status = (view.percent >= READ_THRESHOLD_PERCENT) ? BookStatus::Read : BookStatus::Reading;
    return view;
}

// Wire names, shared by /api/books and POST /api/books/status so the two can
// never drift apart.
inline const char* statusKey(BookStatus status) {
    switch (status) {
        case BookStatus::Unread:
            return "unread";
        case BookStatus::Reading:
            return "reading";
        case BookStatus::Read:
            return "read";
    }
    return "unread";
}

inline const char* overrideKey(StatusOverride override) {
    switch (override) {
        case StatusOverride::Auto:
            return "auto";
        case StatusOverride::Unread:
            return "unread";
        case StatusOverride::Reading:
            return "reading";
        case StatusOverride::Read:
            return "read";
    }
    return "auto";
}

// Parses a wire name into an override. Returns false on anything unrecognised,
// so the endpoint answers 400 instead of silently storing Auto.
inline bool parseOverride(const char* key, StatusOverride& out) {
    if (key == nullptr) return false;
    struct Entry {
        const char* key;
        StatusOverride value;
    };
    static const Entry table[] = {
        {"auto", StatusOverride::Auto},
        {"unread", StatusOverride::Unread},
        {"reading", StatusOverride::Reading},
        {"read", StatusOverride::Read},
    };
    for (const Entry& e : table) {
        const char* a = e.key;
        const char* b = key;
        while (*a != '\0' && *a == *b) {
            ++a;
            ++b;
        }
        if (*a == '\0' && *b == '\0') {
            out = e.value;
            return true;
        }
    }
    return false;
}

// Whether an entry must survive ProgressStore::reconcile() even though its
// .epub is gone from the device.
//
// Without this, tidying finished books off the 10 MB partition would erase the
// record that they were ever read — which is the one thing a reading history
// has to survive. Only an explicit "read" is kept: a derived one cannot be
// evaluated here (the page count cache is scoped to the current font and says
// nothing about a book that is no longer present), and keeping every entry
// would defeat the pruning entirely.
inline bool isReadRecord(StatusOverride override) {
    return override == StatusOverride::Read;
}
