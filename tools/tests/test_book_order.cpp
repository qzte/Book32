// Host test for the shared book-order merge logic.
// Build: g++ -std=c++17 -I ../../lib/Book32_Core -o test_book_order test_book_order.cpp && ./test_book_order
#include "BookOrderLogic.h"
#include <cassert>
#include <string>
#include <vector>
#include <cstdio>

using std::string;
using std::vector;

static vector<string> apply(const vector<string>& order, vector<string> fs) {
    applyBookOrderT(order, fs, [](const string& item, const string& key) { return item == key; });
    return fs;
}

int main() {
    // 1. Ordered entries come first, in saved order.
    {
        auto r = apply({"b.epub", "a.epub"}, {"a.epub", "b.epub", "c.epub"});
        assert((r == vector<string>{"b.epub", "a.epub", "c.epub"}));
    }
    // 2. New files (not in order) appended in FS enumeration order.
    {
        auto r = apply({"a.epub"}, {"x.epub", "a.epub", "y.epub"});
        assert((r == vector<string>{"a.epub", "x.epub", "y.epub"}));
    }
    // 3. Orphans in the order (deleted books) are ignored.
    {
        auto r = apply({"gone.epub", "a.epub"}, {"a.epub", "b.epub"});
        assert((r == vector<string>{"a.epub", "b.epub"}));
    }
    // 4. Empty order leaves FS order untouched.
    {
        auto r = apply({}, {"c.epub", "a.epub"});
        assert((r == vector<string>{"c.epub", "a.epub"}));
    }
    // 5. Duplicate key in order matches only once.
    {
        auto r = apply({"a.epub", "a.epub"}, {"a.epub", "b.epub"});
        assert((r == vector<string>{"a.epub", "b.epub"}));
    }
    // 6. Heterogeneous types: order by key, items are structs (AppReader case).
    {
        struct Entry { string path; };
        vector<string> order = {"b.epub"};
        vector<Entry> books = {{"/a.epub"}, {"/b.epub"}};
        applyBookOrderT(order, books, [](const Entry& e, const string& key) { return e.path == "/" + key; });
        assert(books.size() == 2 && books[0].path == "/b.epub" && books[1].path == "/a.epub");
    }
    printf("All 6 tests passed.\n");
    return 0;
}
