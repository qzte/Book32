#pragma once
// Book32 v1.18.0 — one in-memory "go to chapter" request, set by the web UI
// and consumed by AppReader the next time the matching book is opened. Same
// shape as GoToPercentStore (see that file), but simpler to resolve: the web
// UI already knows the exact chapter index (it came from ChapterTocStore's
// own list), so there is nothing to scan for on open — AppReader jumps
// straight to it (see AppReader::applyChapterJump).
//
// Deliberately not persisted to flash, for the same reason as
// GoToPercentStore: a one-shot intent ("jump the next time I open this
// book"), not state that needs to survive a reboot.
//
// Only one request is kept: setting a new one for book B silently replaces
// an unconsumed request for book A, same accepted simplification as
// GoToPercentStore.

#include <Arduino.h>
#include "Lock.h"

class GoToChapterStore {
  public:
    static GoToChapterStore& getInstance();

    void setPending(const String& originalName, int chapterIndex);

    // If a pending request exists for exactly this book, consumes it
    // (clears the slot) and returns true with `outChapterIndex` set.
    // Otherwise returns false and leaves any pending request for a
    // different book untouched.
    bool takePendingFor(const String& originalName, int& outChapterIndex);

  private:
    GoToChapterStore() {}
    Book32Mutex _mutex;
    bool _hasPending = false;
    String _book;
    int _chapterIndex = 0;
};
