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

// Schema of reader_progress.json. v1 keyed books by path ("/livro.epub") and
// stamped updatedAt = millis(); v2 keys by original filename and uses the
// monotonic `seq` counter instead, which survives reboots.
static const int PROGRESS_SCHEMA_CURRENT = 2;

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
};

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
inline MergeResult mergeProgress(const BookProgress* local,
                                 const BookProgress& imported,
                                 bool fileExists,
                                 BookProgress& out) {
    if (local == nullptr) {
        out = imported;
        out.pending = !fileExists;
        return MergeResult::Added;
    }
    if (imported.globalPage > local->globalPage) {
        out = imported;
        out.pending = !fileExists;
        return MergeResult::Merged;
    }
    out = *local;
    out.pending = !fileExists;
    return MergeResult::KeptLocal;
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
// case the file may still be on its way.
inline bool shouldPrune(const BookProgress& entry, bool fileExists) {
    return !fileExists && !entry.pending;
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
    return schema == 1 || schema == 2;
}
