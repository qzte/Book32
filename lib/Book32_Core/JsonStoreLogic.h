#pragma once
// Book32 — pure logic shared by every JSON-backed store.
//
// No ArduinoJson, no LittleFS, no Arduino String: only plain types and
// templates, so every decision that can be wrong is host-testable
// (tools/tests/test_json_store.cpp). Same pattern as BookOrderLogic.h and
// ProgressMergeLogic.h.
//
// Three things used to be written by hand in each of the ten stores:
//
//   * the ArduinoJson capacity estimate ("file size * 2 + slack" to read,
//     "base + entries * per-entry" to write, both clamped to a ceiling).
//     Ten near-identical copies with different constants and no test on any
//     of them.
//   * the decision table for a save: what counts as a refusal and what
//     counts as success. Only ProgressStore and BookmarkStore got this
//     right; the others treated "the file opened" as "the write worked"
//     (see JsonFileStore.h).
//   * reconcile(): drop stored entries for books that are no longer on the
//     device. Every copy was the same O(entries * books) pair of nested
//     loops.

#include <cstddef>
#include <set>

// --- Capacity ---------------------------------------------------------------
// base + perUnit * units, clamped to maxCapacity. Reads pass the file size as
// `units` (the parsed document needs more room than the text it came from);
// writes pass the entry count.
//
// The multiplication saturates instead of wrapping: a wrapped estimate would
// produce a *small* document for a huge input, and a document too small to
// hold what it is given overflows — which, before the fail-closed checks that
// now cover it, is exactly how a good file got replaced with half a library.
inline size_t jsonStoreCapacity(size_t base, size_t perUnit, size_t units, size_t maxCapacity) {
    size_t cap;
    if (perUnit != 0 && units > (maxCapacity / perUnit)) {
        cap = maxCapacity; // would exceed the ceiling anyway, saturate early
    } else {
        cap = base + perUnit * units;
    }
    if (cap > maxCapacity) cap = maxCapacity;
    return cap;
}

// --- Save outcome -----------------------------------------------------------
// What a store learns from one attempt at writing its file. Kept separate from
// the filesystem calls so the decision table can be tested without a
// filesystem: JsonFileStore.cpp performs the steps and asks this function what
// they mean.
enum class JsonWriteOutcome {
    Ok,
    RefusedOverflow,   // the document did not fit its capacity — nothing written
    RefusedOpen,       // the temp file would not open
    RefusedShortWrite, // fewer bytes reached the temp file than the document holds
    RefusedRename      // the temp file is written but could not replace the target
};

struct JsonWriteAttempt {
    bool overflowed = false; // JsonDocument::overflowed()
    bool opened = false;     // temp file opened for writing
    size_t expected = 0;     // measureJson(doc)
    size_t written = 0;      // bytes serializeJson() reported
    bool renamed = false;    // temp -> target rename succeeded
};

// Fail-closed at every step: anything short of "the whole document reached a
// temp file and that file replaced the target" leaves the previous file in
// place. A partially written JSON file does not fail to parse in halves — it
// fails to parse at all, taking every other book's entry with it.
inline JsonWriteOutcome jsonWriteOutcome(const JsonWriteAttempt& attempt) {
    if (attempt.overflowed) return JsonWriteOutcome::RefusedOverflow;
    if (!attempt.opened) return JsonWriteOutcome::RefusedOpen;
    // `expected == 0` would mean an empty document; still a short write if
    // nothing came out, which is why the comparison is not `written == 0`.
    if (attempt.written != attempt.expected) return JsonWriteOutcome::RefusedShortWrite;
    if (!attempt.renamed) return JsonWriteOutcome::RefusedRename;
    return JsonWriteOutcome::Ok;
}

inline const char* jsonWriteOutcomeMessage(JsonWriteOutcome outcome) {
    switch (outcome) {
        case JsonWriteOutcome::Ok:
            return "ok";
        case JsonWriteOutcome::RefusedOverflow:
            return "document exceeded its capacity";
        case JsonWriteOutcome::RefusedOpen:
            return "temp file would not open";
        case JsonWriteOutcome::RefusedShortWrite:
            return "short write (filesystem full?)";
        case JsonWriteOutcome::RefusedRename:
            return "rename failed";
    }
    return "unknown";
}

// --- Reconcile --------------------------------------------------------------
// Drops every stored key that is not in `present`, and reports whether
// anything was dropped (the caller only pays for a save when it was).
//
// Templated on the map and the name list so the device passes
// std::map<String, T> / std::vector<String> and the host test passes
// std::string containers. The set turns what used to be nested loops over
// every stored key times every present book into one ordered lookup per key.
template <class Map, class Names> bool reconcileStoreKeys(Map& stored, const Names& present) {
    using Key = typename Map::key_type;
    std::set<Key> keep(present.begin(), present.end());

    bool changed = false;
    for (auto it = stored.begin(); it != stored.end();) {
        if (keep.find(it->first) == keep.end()) {
            it = stored.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    return changed;
}
