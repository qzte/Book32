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

#include <Arduino.h>
#include <map>

class PageCountStore {
public:
    static PageCountStore& getInstance();

    // Returns 0 when the book has no cached total, or the cache was computed
    // at different reader font settings than (fontSize, fontFamily).
    int get(const String& originalName, int fontSize, int fontFamily);

    // Persists the total for originalName. If (fontSize, fontFamily) differs
    // from what the cache currently holds, every existing entry is dropped
    // first — they were paginated at a font that's no longer in use.
    void set(const String& originalName, int fontSize, int fontFamily, int totalPages);

private:
    PageCountStore() {}
    void load();
    bool save();

    bool _loaded = false;
    int _fontSize = 0;
    int _fontFamily = -1;
    std::map<String, int> _totals;
};
