#pragma once
// Book32 v1.14.0 — /bookmarks.json on EbookFS. Named, saved reading
// positions, distinct from the resume-on-open position ProgressStore already
// tracks: a bookmark stays put while you keep reading past it, so you can
// jump back later. Same atomic-write shape as ProgressStore (see that file's
// header comment for why: capacity from file size, .tmp + rename, a
// monotonic `seq`).
//
// Books are keyed by *original* filename, exactly like ProgressStore, so a
// bookmark set on one device still matches after a library import that
// truncated names differently on another.
//
// Bookmarks are created and jumped-to from the web UI only (see
// docs/plans/2026-08-29-bookmarks-and-goto-percent-design.md for why): adding
// one snapshots whatever position is currently saved in ProgressStore for
// that book, and jumping to one overwrites that saved position — the reader
// always resumes from ProgressStore (AppReader::loadBookProgress), so the
// jump takes effect the next time that book is opened on the device.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <map>
#include <vector>
#include "Lock.h"

struct Bookmark {
    int chapter = 0;
    int nodeIndex = 0;
    int charOffset = 0;
    int globalPage = 1;
    unsigned long seq = 0; // creation order; also the id used to remove/jump
    String label;
};

class BookmarkStore {
  public:
    static BookmarkStore& getInstance();

    void begin();

    // Newest last (creation order). Empty vector if the book has none.
    std::vector<Bookmark> list(const String& originalName);

    // Adds a bookmark at the given position. Returns the new bookmark's seq
    // (always > 0) on success, or 0 if the book already holds
    // MAX_BOOKMARKS_PER_BOOK, the name is empty, or the write failed.
    unsigned long add(const String& originalName, const String& label, int chapter, int nodeIndex,
                      int charOffset, int globalPage);

    // Returns true if a bookmark with that seq existed under that book and
    // was removed.
    bool remove(const String& originalName, unsigned long seq);

    // Drops every bookmark for a book whose .epub is gone. Same shape as
    // ProgressStore::reconcile; called from AppReader::scanBooks alongside it.
    void reconcile(const std::vector<String>& presentOriginalNames);

    size_t count(const String& originalName);

    static const size_t MAX_BOOKMARKS_PER_BOOK = 20;
    static const size_t MAX_LABEL_LEN = 40;

  private:
    BookmarkStore() {}
    bool load();
    bool save();

    Book32Mutex _mutex;
    std::map<String, std::vector<Bookmark>> _books;
    unsigned long _seq = 0;
    bool _loaded = false;
};
