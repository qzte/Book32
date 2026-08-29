#pragma once
// Book32 v1.8.0 — pure reading-progress merge/migration logic.
//
// No ArduinoJson, no LittleFS, no Arduino String: only plain structs and free
// functions, so every decision that can be wrong is host-testable
// (tools/tests/test_progress_merge.cpp). Same pattern as BookOrderLogic.h.
//
// Used by ProgressStore, which owns reader_progress.json and is the single
// writer for both AppReader and WebMgr.

#include <cstddef>
#include <cstdint>
#include "BookStatusLogic.h"

// Schema of reader_progress.json. v1 keyed books by path ("/livro.epub") and
// stamped updatedAt = millis(); v2 keys by original filename and uses the
// monotonic `seq` counter instead, which survives reboots. v3 adds the status
// override and the three reading dates — v2 files decode as Auto with zeroed
// dates, so the migration needs no special case.
static const int PROGRESS_SCHEMA_CURRENT = 3;

// Position inside one book. `chapter` + `nodeIndex` + `charOffset` is a content
// pointer, not a page index: it survives a font size or family change, which a
// page number does not.
struct BookProgress {
    int chapter = 0;
    int nodeIndex = 0;
    int charOffset = 0;
    int globalPage = 1;
    unsigned long seq = 0;
    // Imported for a book whose .epub is not on this device yet. Exempt from
    // pruning so that importing state before uploading the files does not
    // silently throw the state away.
    bool pending = false;

    // v3. What the user said the status is; the status itself is always
    // derived (see BookStatusLogic.h) and never stored.
    StatusOverride override = StatusOverride::Auto;

    // v3. Epoch seconds UTC, 0 = unknown. Unknown is a real state, not a
    // failure: the device has no battery-backed RTC, so after a power cut
    // there is no clock until the next WiFi connection, and a date written in
    // that window would be a lie. See TimeMgr.h.
    uint32_t startedAt = 0;
    uint32_t finishedAt = 0;
    uint32_t lastReadAt = 0;
};

// Merge helpers for the dates. Both treat 0 as "unknown" rather than as a very
// old timestamp, which is why neither is a plain min/max.

// The earlier of two dates, ignoring unknowns.
inline uint32_t olderDate(uint32_t a, uint32_t b) {
    if (a == 0) return b;
    if (b == 0) return a;
    return a < b ? a : b;
}

// The later of two dates, ignoring unknowns.
inline uint32_t newerDate(uint32_t a, uint32_t b) {
    if (a == 0) return b;
    if (b == 0) return a;
    return a > b ? a : b;
}

enum class MergeResult {
    Added,       // no local entry existed
    Merged,      // imported position was further ahead and won
    KeptLocal    // local position was equal or further ahead
};

// Merge one imported entry over the local one, if any.
//
// Rule: the further-ahead globalPage wins; a tie keeps local. Caveat worth
// knowing — globalPage depends on the font size the book was read at, so
// across two devices with different fonts this comparison is approximate.
//
// local may be null (book unknown here). fileExists says whether the matching
// .epub is present on this device, which decides the pending flag.
// A deliberate mark never loses to an absent one: Auto on either side means "no
// opinion", so the other side's opinion stands. When both carry a *different*
// explicit mark the imported one wins — importing is the more recent deliberate
// act, and there is no cross-device clock to order the two by (`seq` is
// device-local, so it cannot be compared across devices).
inline StatusOverride mergeOverride(StatusOverride local, StatusOverride imported) {
    if (imported == StatusOverride::Auto) return local;
    return imported;
}

inline MergeResult mergeProgress(const BookProgress* local,
                                 const BookProgress& imported,
                                 bool fileExists,
                                 BookProgress& out) {
    if (local == nullptr) {
        out = imported;
        out.pending = !fileExists;
        return MergeResult::Added;
    }

    MergeResult result;
    if (imported.globalPage > local->globalPage) {
        out = imported;
        result = MergeResult::Merged;
    } else {
        out = *local;
        result = MergeResult::KeptLocal;
    }

    // Dates and the override describe the book, not the position, so they are
    // reconciled independently of which side's page won. Losing the date you
    // finished a book because the other device happened to sit one page further
    // in would be plainly wrong.
    //
    // `startedAt` and `finishedAt` take the *older* of the two: when you began
    // and ended the book are historical facts, and the earliest device to
    // record one saw it happen. `lastReadAt` takes the newer, since it is the
    // most recent activity anywhere.
    out.startedAt = olderDate(local->startedAt, imported.startedAt);
    out.finishedAt = olderDate(local->finishedAt, imported.finishedAt);
    out.lastReadAt = newerDate(local->lastReadAt, imported.lastReadAt);
    out.override = mergeOverride(local->override, imported.override);

    out.pending = !fileExists;
    return result;
}

// Normalise a stored key to the v2 form: drop any leading path (v1 keys were
// full paths like "/livro.epub"), then resolve the on-disk filename to the
// original long name via `resolve` (getOriginalFilename on device), which
// returns its argument unchanged when there is no metadata entry.
//
// Templated on the string type so the same code serves Arduino String on
// device and std::string in the host test; only c_str() and construction from
// const char* are assumed.
template <typename Str, typename Resolve>
Str migrateProgressKey(const Str& raw, Resolve resolve) {
    const char* s = raw.c_str();
    size_t lastSep = 0;
    for (size_t i = 0; s[i] != '\0'; ++i) {
        if (s[i] == '/' || s[i] == '\\') lastSep = i + 1;
    }
    return resolve(Str(s + lastSep));
}

// An entry whose .epub is gone is dead weight — unless it is pending, in which
// case the file may still be on its way, or explicitly marked read, in which
// case it is the reading history and must outlive the file. Tidying finished
// books off the 10 MB partition is normal housekeeping and must not quietly
// erase the record that they were read.
inline bool shouldPrune(const BookProgress& entry, bool fileExists) {
    return !fileExists && !entry.pending && !isReadRecord(entry.override);
}

// Clear `pending` the first time the file appears. Returns true when it
// actually changed something, so the caller only writes flash when needed.
inline bool syncPending(BookProgress& entry, bool fileExists) {
    if (entry.pending && fileExists) {
        entry.pending = false;
        return true;
    }
    return false;
}

// An unrecognised schema is rejected whole: half-applying a bundle from a
// future firmware would be worse than refusing it.
inline bool isSupportedSchema(int schema) {
    return schema == 1 || schema == 2 || schema == 3;
}
