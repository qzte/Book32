#pragma once
// Book32 — local cache for each book's total page count.
//
// AppReader deliberately never paginates a whole book synchronously (see
// AppReader::openBook): for a large book that would stall opening it. Instead
// it counts a little at a time from update() and, once a book is fully
// counted, stores the result here so the total is known instantly next time
// — in the library list and from the moment the book is reopened.
//
// Page count depends on the font size and family the book was paginated at,
// so the whole cache is scoped to one (fontSize, fontFamily) pair: switching
// fonts invalidates every entry rather than tracking a signature per book.
//
// This is a local performance cache only, unlike ProgressStore — it is not
// part of the state export/import between devices, since a page count means
// nothing on a device reading at a different font size.
//
// Counting a whole book rarely finishes in one sitting: standby (long-press
// KEY2) and the idle-sleep timeout both call esp_deep_sleep_start() directly
// (see BatteryMgr::enterIdleSleep), which wipes RAM without running
// AppReader::closeBook() first. Without a checkpoint, every standby cycle
// would silently discard all progress and AppReader would restart counting
// from chapter 0 on every reopen — for a book read in short sessions between
// standbys, the total might never be known. The checkpoint records the last
// chapter boundary counting reached, so a new session resumes from there
// instead of from the start.

#include <Arduino.h>
#include <map>

struct PageCountCheckpoint {
    int chapter = 0;     // Next chapter to start counting from
    int pagesSoFar = 0;  // Pages counted in chapters before it
};

class PageCountStore {
public:
    static PageCountStore& getInstance();

    // Returns 0 when the book has no cached total, or the cache was computed
    // at different reader font settings than (fontSize, fontFamily).
    int get(const String& originalName, int fontSize, int fontFamily);

    // Persists the total for originalName. If (fontSize, fontFamily) differs
    // from what the cache currently holds, every existing entry (totals and
    // checkpoints alike) is dropped first — they were paginated at a font
    // that's no longer in use. Clears any in-progress checkpoint for the book.
    void set(const String& originalName, int fontSize, int fontFamily, int totalPages);

    // Returns true and fills out when a resumable checkpoint exists at these
    // font settings.
    bool getCheckpoint(const String& originalName, int fontSize, int fontFamily,
                       PageCountCheckpoint& out);

    // Records progress made toward a total that hasn't finished counting yet.
    void setCheckpoint(const String& originalName, int fontSize, int fontFamily,
                       const PageCountCheckpoint& checkpoint);

private:
    PageCountStore() {}
    void load();
    bool save();
    void resetIfFontChanged(int fontSize, int fontFamily);

    bool _loaded = false;
    int _fontSize = 0;
    int _fontFamily = -1;
    std::map<String, int> _totals;
    std::map<String, PageCountCheckpoint> _checkpoints;
};
