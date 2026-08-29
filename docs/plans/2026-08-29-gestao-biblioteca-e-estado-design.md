# Library management, reading status and dates — design notes

Adds three things to the existing local web UI:

1. A view of a **library folder on the PC**, diffed against what is on the
   device, so missing books can be sent and stale ones removed.
2. A per-book **status** — unread / reading N% / read.
3. Per-book **dates** — started, finished, last read.

## Why the PC folder is read-only, and read from the device's own page

A page served over `http://<device-ip>/` cannot use the File System Access
API (`showDirectoryPicker`): that API requires a secure context, and a plain
HTTP origin on a private IP is not one. Serving the page from the existing
GitHub Pages site instead does not help — an HTTPS page calling
`http://192.168.x.x` is blocked as mixed content.

Two options survive:

- **A helper process on the PC** serving `http://localhost:<port>`. Localhost
  *is* a secure context, so it gets full read/write access to the folder and
  can talk to the device over HTTP without mixed content.
- **`<input type="file" webkitdirectory>` on the page the device already
  serves.** This works on plain HTTP. It yields the folder listing (names and
  sizes) without reading any file content, which is all a diff needs.

This design takes the second. The deciding argument is ownership of state:
reading position already lives on the device in `ProgressStore`, and already
leaves it through `/api/library/export`. Status and dates belong next to the
position, not beside the `.epub` files. Once nothing needs to be written into
the PC folder, the helper process's one real advantage disappears and it is
left as a second component to install, run and maintain.

The cost is that the folder handle cannot be persisted, so the folder must be
re-picked to send files. This is softened below.

## Data model

`BookProgress` (`lib/Book32_Core/ProgressMergeLogic.h`) gains four fields and
the schema goes to 3:

```cpp
enum class StatusOverride : uint8_t { Auto = 0, Unread, Reading, Read };

StatusOverride override = StatusOverride::Auto;
uint32_t startedAt  = 0;   // epoch UTC, 0 = unknown
uint32_t finishedAt = 0;
uint32_t lastReadAt = 0;
```

Only the *override* is stored, never the resulting status. The displayed
status is derived on every read by a pure function in a new
`BookStatusLogic.h` — no ArduinoJson, no LittleFS, no Arduino `String`, same
pattern as `ProgressMergeLogic.h` and `GoToPercentLogic.h`, so the decisions
are host-testable:

- no `ProgressStore` entry → **unread**
- `override != Auto` → whatever it says
- `total > 0 && globalPage >= 96% of total` → **read**
- otherwise → **reading**, with a percentage when `PageCountStore` already
  knows the total for the current font settings, and without one when it does
  not.

The 96% threshold exists because a reader rarely lands on the last page:
endnotes, appendices and colophons mean a finished book commonly stops in the
mid-90s and would otherwise never mark itself read.

Migration v2 → v3 needs no special case: existing entries decode as `Auto`
with zeroed dates.

`mergeProgress` keeps the furthest-ahead-`globalPage` rule, and dates travel
with the winning side — except `startedAt`, where the **older** of the two
non-zero values always wins. When a book was started matters regardless of
which device recorded it.

## Clock

The device has no battery-backed RTC (see the note in `ProgressStore.h`), but
it does have the ESP32 internal clock, and ESP-IDF preserves system time
across deep sleep. One NTP sync per power cycle is therefore enough.

A new `TimeMgr` in Core:

- calls `configTime()` against NTP when WiFi comes up;
- exposes `bool nowEpoch(uint32_t& out)`, returning `false` while the clock
  is not yet plausible (before 2020).

After a full power loss the clock is unknown until the next WiFi connection.
During that window dates are written as 0 and rendered as `—`. **A date is
never invented.**

The awkward case — finishing a book while the clock is dark — is covered for
free by the override: when the browser marks a book read it sends its own
timestamp, and the browser's clock is always good.

## Where the firmware writes

`AppReader::saveReadingProgress` (`lib/Apps/AppReader/AppReader.cpp:505`)
builds a fresh `BookProgress` and calls `ProgressStore::set()`, which replaces
the whole entry. Left alone, that would **erase the override and the dates on
every page turn**.

The fix is not in `AppReader`. `ProgressStore::set()` preserves `override`,
`startedAt` and `finishedAt` from the existing entry, and stamps `lastReadAt`
itself (and `startedAt`, the first time). `AppReader` stays unaware that dates
exist, which is the cleaner seam.

`finishedAt` is the exception: deciding it needs the total page count, which
`set()` does not have. `set()` takes an added `int totalPages = 0` and
`AppReader` passes the `_totalPages` it already maintains. Still one write.

`fillExportJson` and `applyImportedJson` carry the new fields. Status and
dates are portable between devices, unlike `pending` and `seq`, which stay
device-local and excluded as before.

### Pruning

`ProgressStore::reconcile()` drops entries whose `.epub` is gone. Today that
costs a reading position. With this change, tidying books off the device would
also erase the record that they were read. Entries whose status is **read**
are therefore exempt from pruning, alongside `pending` — that exemption is
what lets a "books I have read" list survive housekeeping.

## API

`/api/books` (already the list's only call) gains per book:

```json
{ "status": "reading", "percent": 43,
  "startedAt": 1756400000, "finishedAt": 0, "lastReadAt": 1756480000 }
```

`percent` is `null` while the total is uncounted. No extra round trip, and the
cost is small: `ProgressStore` and `PageCountStore` are already in-RAM maps.

New `POST /api/books/status`, body `{filename, status, at}`:

- `status` is one of `auto` / `unread` / `reading` / `read`;
- `at` is the browser's epoch, used for `finishedAt` when marking read;
- `filename` goes through the existing `isSafeBookName` allow-list;
- 404 when no such book, 400 on an invalid status.

Marking a never-opened book as read creates an entry with `globalPage` at 1.
That is legitimate — it is exactly the "read it elsewhere" case.

## Web UI

**Your Books.** Each row gains a status badge (*Unread* / *Reading 43%* /
*Read*) and a quiet line of dates. The override is a four-value `<select>`
including *Automatic*, so a manual mark can always be handed back to the
derivation instead of being permanent. Above the list, a status filter and a
sort-by-date control — neither touches the existing manual order, which is
the order shown *on the device* and stays independent of this view.

**PC Library (new section).** An `<input type="file" webkitdirectory>` picks
the folder; the browser produces the listing without reading file contents.
Matching against `/api/books` is by **original filename**, which is precisely
what `BookMeta` stores before upload truncation. Three groups are shown:

- *Only on PC* — with **Send**
- *On both*
- *Only on the reader* — with **Delete**

Sending reuses the existing upload endpoint, serially.

Two specific guards:

- Check free space via `/api/fs` before a batch send. The ebook partition is
  10 MB and is easy to overrun.
- Matching is by name, so renaming a book on the PC makes it look new. Sizes
  are shown alongside so an obvious rename is recognisable.

**Recovering the persistence that option A gives up.** The last folder
listing (names and sizes only) is kept in `localStorage`. Reopening the page
still shows the diff, labelled with when it was taken; the folder only needs
re-picking to actually send files.

Where `webkitdirectory` is unsupported (mobile browsers), the section hides
itself rather than failing.

## Testing

Host tests, in the mould of the existing ones under `tools/tests/`:

- `test_book_status.cpp` — the derivation table: no entry; each override
  value; the 95 / 96 / 100 percent boundaries; `total == 0`. Plus the merge
  rules for dates, including oldest-`startedAt`-wins.
- `test_progress_merge.cpp` — extended for the v2 → v3 migration and for
  round-tripping the new fields through export/import.

Not coverable on the host, so recorded in `TODO.txt` as on-device checks:
batch sending against a nearly full partition, a real NTP sync and its
survival across standby, the 96% threshold on a large book, and confirming
that a page turn does not wipe a manual override.
