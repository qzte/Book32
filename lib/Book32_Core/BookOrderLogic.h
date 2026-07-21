#pragma once
// Book32 v1.2.0 — shared manual book-order merge logic.
//
// Merge rule: entries listed in the saved order (and still present) come
// first, in saved order; remaining items keep filesystem enumeration order;
// orphaned order entries (deleted books) are ignored.
//
// Pure template, no Arduino dependencies — host-testable
// (tools/tests/test_book_order.cpp). Used by WebMgr (/api/books) and
// AppReader::scanBooks() so web UI and e-ink library always agree.

#include <vector>

// order: saved keys (e.g. "book.epub"). items: list to reorder in place.
// matches(item, key) -> true when the item corresponds to that order key.
template <typename OrderVec, typename ItemVec, typename Matches>
void applyBookOrderT(const OrderVec& order, ItemVec& items, Matches matches) {
    ItemVec result;
    result.reserve(items.size());
    for (const auto& key : order) {
        for (auto it = items.begin(); it != items.end(); ++it) {
            if (matches(*it, key)) {
                result.push_back(*it);
                items.erase(it);
                break;
            }
        }
    }
    for (auto& rest : items) result.push_back(rest);
    items = result;
}
