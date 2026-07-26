# KEY1: debounce floor, shared with KEY2

Date: 2026-07-26
Version: 1.8.1 (patch: bug fix, no behaviour change for a clean press)

## Problem

KEY1 and KEY2 are both read with a bare `digitalRead()` inside
`InputMgr::inputTask()`. `btnBack.tick()` and `btnSleep.tick()` are never
called, so the `setDebounceMs(30)` configured on those OneButton objects never
runs. Debouncing has to happen in the polling code itself.

KEY2 did that. Its release branch required `pressDuration >= 30`, with a comment
explaining why: bounce would otherwise cost a ~2s full e-ink refresh.

KEY1 did not. Its release branch only checked `pressDuration < 400`, with no
lower bound. A contact rebound on release therefore looked like a second,
very short press: `_btnBackPressTime` reset to 0, the pin read LOW again, and a
second `INPUT_PREV` was enqueued. The reader went back two pages on one press.
The queue holds 8 entries, so nothing was dropped — both events were dispatched.

The bug was invisible in review because the two buttons had the same structure
and only differed by one comparison, twenty lines apart.

## Approach

Three options were considered.

1. **Add `pressDuration >= 30` to KEY1.** One line, fixes the bug. Rejected:
   leaves two copies of the same rule and the same trap for the next button.
2. **Call `tick()` on both and use OneButton's debounce.** Rejected: reverts a
   deliberate earlier decision, and the manual polling exists because KEY1 and
   KEY2 need press-side long-press detection that fires while still held.
3. **Extract the rule into a shared pure function.** Chosen.

## Design

New header `lib/Book32_Core/ButtonPressLogic.h`, following the existing
`*Logic.h` pattern (`BookOrderLogic.h`, `ProgressMergeLogic.h`): no Arduino
dependencies, host-testable.

```
classifyButtonRelease(pressDuration, longPressSent) -> IGNORE | CLICK
```

Rules, in order:

| Condition | Result | Why |
| --- | --- | --- |
| `longPressSent` | IGNORE | Press side already owned the event |
| `< 30ms` | IGNORE | Contact bounce |
| `>= 400ms` | IGNORE | Long-press territory |
| otherwise | CLICK | Real tap |

`BUTTON_DEBOUNCE_MIN_MS` and `BUTTON_LONG_PRESS_MS` now also feed the OneButton
setters, so the two thresholds have one definition each in the whole project.

### Behaviour changes

- **KEY1 release under 30ms:** was `INPUT_PREV`, now ignored. This is the fix.
- **KEY2 release at or beyond 400ms with `longPressSent` false:** was
  `INPUT_REFRESH`, now ignored. Unreachable in practice, since
  `enterStandby()` never returns. Documented rather than silently relied upon.
- Everything else is byte-for-byte equivalent. A clean press on any of the
  three keys behaves exactly as in 1.8.0.

## Testing

`tools/tests/test_button_press.cpp`, host-compiled, 7 assertions covering the
bounce floor and both of its boundaries, the long-press threshold and its
boundary, and the `longPressSent` guard overriding duration. Written before the
implementation and confirmed failing first.

```
g++ -std=c++17 -I ../../lib/Book32_Core -o test_button_press test_button_press.cpp
./test_button_press
```

On-hardware checks still to run:

1. Tap KEY1 repeatedly and fast in a book. Each tap goes back exactly one page.
2. Hold KEY1 past 400ms. Main menu, and no stray `INPUT_PREV` on release.
3. Tap KEY2 in a book. One full refresh per tap, never two.
4. Hold KEY2. Standby still engages; KEY3 wakes.
5. KEY3 click and long press unchanged.
6. Serial log shows `KEY1: Button released after N ms` without a following
   `INPUT: KEY1 Click -> PREV` when N is under 30.

## Files changed

- `lib/Book32_Core/ButtonPressLogic.h` — new, shared classifier
- `tools/tests/test_button_press.cpp` — new, host test
- `lib/Book32_Core/InputMgr.cpp` — both release branches use the classifier;
  magic 30/400 replaced by the shared constants
- `lib/Book32_Core/InputMgr.cpp.bk` — deleted (stale committed backup)
- `include/Config.h` — `SYSTEM_VERSION` 1.8.0 to 1.8.1
