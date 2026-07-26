# KEY2: full refresh on click, standby on long press

Date: 2026-07-26
Version: 1.7.0 (minor: new user-facing behaviour, no breaking change)

## Problem

KEY2 (`PIN_BUTTON_SLEEP`, GPIO 3) had a single behaviour: long press to enter
standby. Its short click did nothing.

Meanwhile the reader only clears e-ink ghosting on a schedule, driven by
`_pageTurnsSinceRefresh >= _refreshEveryNPages`. When ghosting becomes annoying
before that counter trips, the reader has no way to ask for a clean repaint. The
only workarounds were to turn enough pages to reach the threshold, or to leave
and re-enter the book.

This change gives the idle short click a job: force a full refresh.

## Behaviour

| Input | Action |
| --- | --- |
| KEY2 click (30ms to 400ms) | Full repaint of the current screen |
| KEY2 long press (>=400ms) | Standby (unchanged) |

Standby deliberately stays on the long press. Moving it to the click would mean
a knocked button drops the device into deep sleep mid-page.

## Design

The refresh reuses `App::forceRedraw()`, which already exists in
`lib/Book32_Core/BaseApp.h`. It was added for the rotation flip and does exactly
what is needed: repaint the whole screen without losing state. All three apps
implement it.

Flow:

1. `InputMgr::inputTask()` sees KEY2 released after 30-400ms and enqueues
   `INPUT_REFRESH`.
2. `InputMgr::update()`, running on the main loop, dequeues it, consumes it
   locally, and calls `forceRedraw()` on the current app.
3. `forceRedraw()` only sets dirty flags. The repaint happens in the next
   `AppMgr::draw()`.

### Why the action is intercepted, not dispatched

A manual full refresh is a display concern, not an app command. Consuming it in
`InputMgr::update()` means:

- No app needs to change. Zero edits to `AppReader`, `AppMainMenu`,
  `AppSettings`.
- Every screen gets it, including modals like the unsaved-changes prompt, which
  would otherwise drop an unrecognised action.

This mirrors how `enterStandby()` already reaches through the `App*` base
interface to call `stop()` without depending on app internals.

### Why the queue, and not a direct call

The input task must not touch the display. Enqueuing moves the work to the main
loop, so the repaint never races the `firstPage()`/`nextPage()` cycle over SPI,
and the ~2 second e-ink refresh never blocks button polling.

## Traps

- **`digitalRead()` has no debounce.** KEY2 is polled directly, so
  `btnSleep.setDebounceMs(30)` never applies to it. Contact bounce on release
  would register as a click and cost a full refresh. Hence the explicit 30ms
  floor on press duration.
- **Enum ordering is load-bearing for logs.** `InputAction` values are printed
  raw by `InputMgr::update()`. `INPUT_REFRESH` is appended after `INPUT_SLEEP`
  so old serial traces keep their meaning.
- **`forceRedraw()` does not reset `_pageTurnsSinceRefresh`.** It doesn't need
  to: `drawReading()` resets the counter inside the branch that
  `_readingFirstDraw` triggers. Adding a reset to `forceRedraw()` would be
  redundant.
- **The reading position is safe.** `forceRedraw()` invalidates the render cache
  but leaves `_currentPagePointer` untouched. Pagination from the same pointer
  with the same font metrics is deterministic, so the same page comes back.
- **The long-press guard is currently unreachable.** `enterStandby()` never
  returns, so a long press never reaches the release branch. The
  `_btnSleepLongPressSent` check is kept anyway: if standby ever becomes
  cancellable, releasing the button must not fire a stray refresh.

## Testing

The change is GPIO timing plus a queue hop, with no pure logic worth extracting
into a host harness. Verified on hardware:

1. In a book, tap KEY2. The screen flashes fully and returns to the same page.
2. Tap KEY2 in the library, the main menu, and the settings screens. Each
   repaints without losing selection or draft edits.
3. In settings with an unsaved edit, tap KEY2. The screen repaints and the edit
   survives.
4. Hold KEY2 for over 400ms. Standby still engages; KEY3 wakes the device.
5. Tap KEY2 repeatedly and fast. No queue overflow, no doubled refresh.
6. Watch the serial log for `KEY2: Button released after N ms -> REFRESH` and
   `INPUT: KEY2 Click -> FULL REFRESH`.

## Files changed

- `lib/Book32_Core/InputMgr.h` — `INPUT_REFRESH` appended to `InputAction`
- `lib/Book32_Core/InputMgr.cpp` — release-branch detection, dispatch
  interception, updated comments
- `include/Config.h` — `SYSTEM_VERSION` 1.6.4 to 1.7.0, `PIN_BUTTON_SLEEP`
  comment
- `README.md` — Controls section now documents all three keys
