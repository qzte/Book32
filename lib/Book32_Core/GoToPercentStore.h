#pragma once
// Book32 v1.14.0 — one in-memory "go to %" request, set by the web UI and
// consumed by AppReader the next time the matching book is opened.
//
// Deliberately not persisted to flash: this is a one-shot intent ("jump the
// next time I open this book"), not state that needs to survive a reboot —
// if the device restarts before the book is opened, forgetting the request
// is the right behaviour, same as a request that's simply never acted on.
// That also means no atomic-write ceremony is needed here, unlike
// ProgressStore/BookmarkStore, which do persist.
//
// Only one request is kept: setting a new one for book B silently replaces
// an unconsumed request for book A. That is an accepted simplification — the
// natural use is "I want to jump in whichever book I open next" — not a
// queue of unrelated deferred jumps.

#include <Arduino.h>
#include "Lock.h"

class GoToPercentStore {
  public:
    static GoToPercentStore& getInstance();

    void setPending(const String& originalName, int percent);

    // If a pending request exists for exactly this book, consumes it
    // (clears the slot) and returns true with `outPercent` set. Otherwise
    // returns false and leaves any pending request for a different book
    // untouched.
    bool takePendingFor(const String& originalName, int& outPercent);

  private:
    GoToPercentStore() {}
    Book32Mutex _mutex;
    bool _hasPending = false;
    String _book;
    int _percent = 0;
};
