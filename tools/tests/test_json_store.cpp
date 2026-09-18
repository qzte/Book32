// Host test for the pure JSON-store logic shared by every store.
// Build: g++ -std=c++17 -I ../../lib/Book32_Core -o test_json_store test_json_store.cpp && ./test_json_store
#include "JsonStoreLogic.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <map>
#include <string>
#include <vector>

using std::string;

static void testCapacity() {
    // Read shape: file size * 2 + slack, under the ceiling.
    assert(jsonStoreCapacity(512, 2, 1000, 65536) == 2512);
    // Write shape: base + entries * per-entry.
    assert(jsonStoreCapacity(1024, 96, 10, 131072) == 1984);
    // Clamped at the ceiling.
    assert(jsonStoreCapacity(512, 2, 1000000, 65536) == 65536);
    // Exactly at the ceiling stays there rather than tipping over it.
    assert(jsonStoreCapacity(0, 2, 32768, 65536) == 65536);
    // An empty file/library still gets the base.
    assert(jsonStoreCapacity(256, 48, 0, 16384) == 256);
    // perUnit == 0 must not divide by zero.
    assert(jsonStoreCapacity(300, 0, 99999, 16384) == 300);

    // Saturation: a units value big enough to wrap size_t must clamp to the
    // ceiling, never come back small. This is the case that would hand a
    // huge library a tiny document and overflow it.
    const size_t huge = std::numeric_limits<size_t>::max() / 2;
    assert(jsonStoreCapacity(1024, 96, huge, 131072) == 131072);
    assert(jsonStoreCapacity(1024, 2, std::numeric_limits<size_t>::max(), 65536) == 65536);
}

static void testWriteOutcome() {
    JsonWriteAttempt ok;
    ok.overflowed = false;
    ok.opened = true;
    ok.expected = 420;
    ok.written = 420;
    ok.renamed = true;
    assert(jsonWriteOutcome(ok) == JsonWriteOutcome::Ok);

    // Overflow is checked before anything touches the filesystem.
    {
        JsonWriteAttempt a = ok;
        a.overflowed = true;
        a.opened = false;
        a.written = 0;
        a.renamed = false;
        assert(jsonWriteOutcome(a) == JsonWriteOutcome::RefusedOverflow);
    }
    {
        JsonWriteAttempt a = ok;
        a.opened = false;
        assert(jsonWriteOutcome(a) == JsonWriteOutcome::RefusedOpen);
    }
    // The regression this whole change is about: the filesystem is full, the
    // serializer got half the document out, and the old code called that a
    // successful save.
    {
        JsonWriteAttempt a = ok;
        a.written = 200;
        assert(jsonWriteOutcome(a) == JsonWriteOutcome::RefusedShortWrite);
    }
    {
        JsonWriteAttempt a = ok;
        a.written = 0;
        assert(jsonWriteOutcome(a) == JsonWriteOutcome::RefusedShortWrite);
    }
    // More written than measured is just as wrong as less.
    {
        JsonWriteAttempt a = ok;
        a.written = 421;
        assert(jsonWriteOutcome(a) == JsonWriteOutcome::RefusedShortWrite);
    }
    {
        JsonWriteAttempt a = ok;
        a.renamed = false;
        assert(jsonWriteOutcome(a) == JsonWriteOutcome::RefusedRename);
    }
    // An empty document that really did write zero bytes is not a short write.
    {
        JsonWriteAttempt a = ok;
        a.expected = 0;
        a.written = 0;
        assert(jsonWriteOutcome(a) == JsonWriteOutcome::Ok);
    }

    // Every outcome has a message, and only Ok says "ok".
    assert(string(jsonWriteOutcomeMessage(JsonWriteOutcome::Ok)) == "ok");
    assert(string(jsonWriteOutcomeMessage(JsonWriteOutcome::RefusedShortWrite)) != "ok");
    assert(string(jsonWriteOutcomeMessage(JsonWriteOutcome::RefusedRename)) != "ok");
}

static void testReconcile() {
    // Books that are gone are dropped; the rest are untouched.
    {
        std::map<string, std::vector<int>> stored = {
            {"a.epub", {1, 2}},
            {"b.epub", {3}},
            {"c.epub", {4, 5, 6}},
        };
        std::vector<string> present = {"a.epub", "c.epub"};
        assert(reconcileStoreKeys(stored, present) == true);
        assert(stored.size() == 2);
        assert(stored.count("b.epub") == 0);
        assert(stored["c.epub"].size() == 3);
    }
    // Nothing removed means no save is spent.
    {
        std::map<string, int> stored = {{"a.epub", 1}, {"b.epub", 2}};
        std::vector<string> present = {"b.epub", "a.epub", "extra.epub"};
        assert(reconcileStoreKeys(stored, present) == false);
        assert(stored.size() == 2);
    }
    // An empty device library clears the store.
    {
        std::map<string, int> stored = {{"a.epub", 1}};
        std::vector<string> present;
        assert(reconcileStoreKeys(stored, present) == true);
        assert(stored.empty());
    }
    // An empty store with books present is a no-op, not a crash.
    {
        std::map<string, int> stored;
        std::vector<string> present = {"a.epub"};
        assert(reconcileStoreKeys(stored, present) == false);
        assert(stored.empty());
    }
    // Duplicates in the present list are harmless.
    {
        std::map<string, int> stored = {{"a.epub", 1}, {"z.epub", 2}};
        std::vector<string> present = {"a.epub", "a.epub"};
        assert(reconcileStoreKeys(stored, present) == true);
        assert(stored.size() == 1 && stored.count("a.epub") == 1);
    }
}

int main() {
    testCapacity();
    testWriteOutcome();
    testReconcile();
    printf("test_json_store: OK\n");
    return 0;
}
