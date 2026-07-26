// Host test for the pure progress merge/migration logic (v1.7.0).
// Build: g++ -std=c++17 -I ../../lib/Book32_Core -o test_progress_merge test_progress_merge.cpp && ./test_progress_merge
#include "ProgressMergeLogic.h"
#include <cassert>
#include <map>
#include <string>
#include <cstdio>

using std::string;

static BookProgress at(int chapter, int node, int page) {
    BookProgress p;
    p.chapter = chapter;
    p.nodeIndex = node;
    p.globalPage = page;
    return p;
}

// Stands in for getOriginalFilename(): truncated name -> original long name.
static string resolveOriginal(const string& name) {
    static const std::map<string, string> meta = {
        {"Os Maias - Eca de Queiro.epub", "Os Maias - Eca de Queiros (integral).epub"},
    };
    auto it = meta.find(name);
    return it == meta.end() ? name : it->second;
}

int main() {
    // 1. Imported further ahead wins.
    {
        BookProgress local = at(2, 10, 100);
        BookProgress imported = at(7, 152, 214);
        BookProgress out;
        assert(mergeProgress(&local, imported, true, out) == MergeResult::Merged);
        assert(out.globalPage == 214 && out.chapter == 7 && out.nodeIndex == 152);
    }
    // 2. Local further ahead wins.
    {
        BookProgress local = at(7, 152, 214);
        BookProgress imported = at(2, 10, 100);
        BookProgress out;
        assert(mergeProgress(&local, imported, true, out) == MergeResult::KeptLocal);
        assert(out.globalPage == 214 && out.chapter == 7);
    }
    // 3. Tie keeps local (no pointless write, no drift).
    {
        BookProgress local = at(3, 40, 100);
        BookProgress imported = at(9, 999, 100);
        BookProgress out;
        assert(mergeProgress(&local, imported, true, out) == MergeResult::KeptLocal);
        assert(out.chapter == 3 && out.nodeIndex == 40);
    }
    // 4. Import-only book with no local file is added as pending.
    {
        BookProgress imported = at(1, 5, 12);
        BookProgress out;
        assert(mergeProgress(nullptr, imported, false, out) == MergeResult::Added);
        assert(out.pending && out.globalPage == 12);
    }
    // 5. Import-only book whose file is already here is not pending.
    {
        BookProgress imported = at(1, 5, 12);
        BookProgress out;
        assert(mergeProgress(nullptr, imported, true, out) == MergeResult::Added);
        assert(!out.pending);
    }
    // 6. Local-only entries are untouched by a merge that does not mention them.
    {
        BookProgress local = at(4, 60, 77);
        BookProgress out;
        assert(mergeProgress(&local, local, true, out) == MergeResult::KeptLocal);
        assert(out.chapter == 4 && out.nodeIndex == 60 && out.globalPage == 77);
    }
    // 7. v1 keys carried a leading slash; v2 keys do not.
    {
        assert(migrateProgressKey<string>("/livro.epub", resolveOriginal) == "livro.epub");
        assert(migrateProgressKey<string>("livro.epub", resolveOriginal) == "livro.epub");
    }
    // 8. A truncated v1 key resolves to the original long name.
    {
        assert(migrateProgressKey<string>("/Os Maias - Eca de Queiro.epub", resolveOriginal) ==
               "Os Maias - Eca de Queiros (integral).epub");
    }
    // 9. Pending entries survive pruning — the .epub may still be on its way.
    {
        BookProgress e = at(1, 0, 5);
        e.pending = true;
        assert(!shouldPrune(e, false));
    }
    // 10. Missing file and not pending: prune. Safety net for books that left
    //     by some path other than /api/books/delete, which already cleans up.
    {
        BookProgress e = at(1, 0, 5);
        assert(shouldPrune(e, false));
        assert(!shouldPrune(e, true));
    }
    // 11. Pending clears the first time the file shows up.
    {
        BookProgress e = at(1, 0, 5);
        e.pending = true;
        assert(syncPending(e, true));
        assert(!e.pending);
        assert(!syncPending(e, true));  // idempotent
    }
    // 12. Unknown schema rejects the whole bundle rather than half-applying it.
    {
        assert(isSupportedSchema(1));
        assert(isSupportedSchema(2));
        assert(!isSupportedSchema(3));
        assert(!isSupportedSchema(0));
    }

    printf("test_progress_merge: all assertions passed\n");
    return 0;
}
