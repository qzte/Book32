# Bookmarks and "go to %" — design notes (v1.14.0)

Implements gaps #1 and #4 from
[2026-08-29-avaliacao-crosspoint-reader.md](2026-08-29-avaliacao-crosspoint-reader.md):
named bookmarks and jumping to an approximate percent into a book.

## Why these are web-managed, not an on-device gesture

Book32 has exactly three physical inputs, and all three are already fully
committed to well-tuned, hardware-verified behaviour:

- KEY3: click = next page, long press = select/close book.
- KEY1: click = previous page, long press = go to main menu.
- KEY2: click = manual full refresh, long press = standby (off by default;
  `TODO.txt` already earmarks it for a future standby feature — squatting on
  it here would conflict with that).

There is no spare gesture. Adding one would mean either changing an existing
button's timing (risking exactly the class of bug this codebase has spent
real effort fixing — see the idle-sleep overflow and standby-guard history)
or introducing a new tiered long-press, which this session has no physical
device to verify. `INPUT_BACK` exists in the `InputAction` enum and is
handled in a couple of `switch` statements, but nothing on the input task
ever enqueues it — it's already dead, not a gesture waiting to be wired up.

Both features are instead managed entirely from the existing local web UI,
using the same shape the codebase already relies on for library reorder and
progress import/export: **the web sets an intent, the device applies it the
next time the affected book is opened.** This works because WiFi (and the
web server) is only ever on outside the reader app — `AppReader::start()`
powers it down unconditionally — so there is no scenario where the web UI
and an open book are both live at once to reconcile.

## Bookmarks

A bookmark is a named, saved position that survives you reading past it —
distinct from `ProgressStore`'s resume-on-open position, which keeps moving
forward. `BookmarkStore` (`lib/Book32_Core/BookmarkStore.h/.cpp`) persists
them to `/bookmarks.json` on `EbookFS`, same atomic `.tmp` + rename shape as
`ProgressStore`, keyed by original filename, capped at 20 per book.

- **Add**: snapshots whatever position `ProgressStore` currently holds for
  that book. No EPUB parsing needed — it's just a copy of already-known
  state, so it's a plain JSON read/write, safe to run inline on the async web
  server task.
- **Jump**: overwrites `ProgressStore`'s saved position for that book with
  the bookmark's. `AppReader::loadBookProgress()` already restores from
  `ProgressStore` unconditionally on open, so this "just works" with zero
  changes to the reader.

## Go to %

Unlike a bookmark, a percent target doesn't correspond to any position
Book32 already knows — it has to be computed from the book's content, which
means parsing the EPUB, which only `EpubLoader` (used exclusively by
`AppReader`) can do. So the web server can't resolve this itself; it only
records the request (`GoToPercentStore`, in-memory only — see that header
for why it's not persisted to flash). `AppReader::openBook()` checks for a
pending request matching the book being opened and, if found, runs the
resolution in the background.

**Why content-length proportion, not an exact page.** The reader has no
persisted per-page index — `_pageHistory` is in-memory, current-chapter-only,
and cleared on every chapter change (see `AppReader.h`). Computing an exact
"page N of totalPages" would mean re-paginating the whole book, which is the
same cost `startTotalPagesCounting()` already avoids doing synchronously.
Content-length proportion is far cheaper: walk each chapter's already-parsed
text once (no font measurement, no rendering) to get its character count,
then place the target percent inside whichever chapter holds that fraction
of the total. It lands close, not exact — the standard trade for a "go to %"
feature, and the only page number displayed afterwards
(`AppReader::updatePercentSeek`) is explicitly a best-effort estimate, not
presented as precise. This is a different situation from the library list's
existing "no percentage: paginating the whole book is too slow, so a
percentage would be made up" rule — that rule is about *displaying* a number
as if it were exact; here the user explicitly asked for an approximate jump,
which is what every "go to %" feature actually does.

The math (`resolvePercentTarget`, `resolveNodeTarget`) lives in
`lib/Book32_Core/GoToPercentLogic.h` as pure functions over plain
`std::vector`s — no `EpubLoader`, no `Arduino.h` — so it's host-testable
(`tools/tests/test_go_to_percent.cpp`) independently of the device-side
glue in `AppReader`.

**Budgeting.** Even without font measurement, scanning every chapter of a
large book could take a non-trivial slice of wall time. `AppReader` runs it
the same way it already runs total-page counting: a bounded slice per
`update()` tick (`TOTAL_PAGES_BUDGET_MS`), so it can't stall the main loop.
It runs independently of (and concurrently with) the existing total-page
counter — different state, no shared renderer.

**Avoiding a double refresh.** If a percent-seek is pending when a book
opens, `openBook()` skips its normal `_needsRedraw = true` — the reader
would otherwise draw the just-restored position once, then draw again a
moment later once the seek resolves, costing two full e-ink refreshes
instead of one and briefly showing the wrong page. Input is also ignored
while a seek is resolving (`AppReader::handleInput`), so a page turn can't
land on a position that's about to be replaced.

## What's out of scope here

- An on-device bookmarks browser or in-reader "go to %" prompt — would need
  the free gesture this device doesn't have (see above).
- Caching chapter lengths for repeat percent-jumps on the same book — the
  scan is cheap enough (parsing only, no rendering) that this wasn't judged
  worth the added state; can be revisited if it proves slow in practice.

## Verification

This session has no physical Book32 hardware and PlatformIO's package
registry (`api.registry.platformio.org` and related) is blocked by this
environment's egress policy, so `pio run` could not be exercised here — only
the pure-logic host tests (`tools/tests/test_go_to_percent.cpp`), a manual
line-by-line review against the existing compiling code's exact patterns
(`ProgressStore`, `updateTotalPagesCount`, the `AsyncCallbackJsonWebHandler`
endpoints already in `WebMgr.cpp`), `node --check` on `data/script.js`, and
`clang-format`. See `TODO.txt` for the on-device checks this still needs,
following the same "a verificar no dispositivo" convention already used
there for v1.8.0.
