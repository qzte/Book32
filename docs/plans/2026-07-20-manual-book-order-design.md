# Manual Book Ordering (v1.2.0)

## Rationale
The library previously listed books in raw LittleFS enumeration order. Users
wanted explicit control: move books up/down with ▲/▼ buttons in the web UI,
with the same order applied on the device's e-ink library.

## Design
- **Persistence**: `/book_order.json` on SystemFS (`spiffs` partition):
  `{"order":["a.epub","b.epub"]}`. Same pattern as `books_meta.json`.
- **Merge rule** (single source of truth in
  `lib/Book32_Core/BookOrderLogic.h`, template `applyBookOrderT`):
  1. Entries in the saved order that still exist come first, in saved order.
  2. Files not in the order (new uploads) are appended in FS enumeration order.
  3. Orphaned order entries (deleted books) are ignored.
- **Endpoints**:
  - `GET /api/books` — books sorted by the merge rule; `.ttf` fonts appended
    after books, unordered. JSON is streamed manually (O(1) memory), fixing the
    old fixed `DynamicJsonDocument(2048)` truncation risk.
  - `POST /api/books/order` — receives the full `order` array; only non-empty
    `.epub` names are accepted.
  - `DELETE /api/books/delete` — also calls `removeFromBookOrder()`.
- **Device**: `AppReader::scanBooks()` loads the same file and applies the same
  template, matching `BookEntry.path == "/" + key`.
- **Frontend**: ▲/▼ per book (fonts excluded); reorders locally and POSTs the
  full order with ~500 ms debounce to limit flash writes.

## Testing
Host test `tools/tests/test_book_order.cpp` (pure C++17, no hardware):

```
cd tools/tests
g++ -std=c++17 -I ../../lib/Book32_Core -o test_book_order test_book_order.cpp
./test_book_order   # "All 6 tests passed."
```

Covers: saved-order priority, new-file append, orphan tolerance, empty order,
duplicate keys, heterogeneous item types (AppReader `BookEntry` case).

## Traps for future maintainers
- Keep the merge logic ONLY in `BookOrderLogic.h`. WebMgr and AppReader must
  both delegate to it — divergence silently desynchronises web UI and e-ink.
- `book_order.json` lives on SystemFS (spiffs), not EbookFS — an `uploadfs`
  of the `spiffs` image wipes it.
- The streamed JSON in `/api/books` uses `jsonEscape()`; any new field must go
  through it too.

## Versioning
SemVer: backwards-compatible feature ⇒ minor bump. `SYSTEM_VERSION` moved from
`"1.0"` (non-SemVer) to `"1.2.0"` (1.1.0 = Latin-1 font work).
