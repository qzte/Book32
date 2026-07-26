// Host test for the shared button release classification logic.
// Build: g++ -std=c++17 -I ../../lib/Book32_Core -o test_button_press test_button_press.cpp && ./test_button_press
#include "ButtonPressLogic.h"
#include <cassert>
#include <cstdio>

int main() {
    // 1. Instant release is contact bounce, not a click. This is the KEY1 bug:
    //    without a floor, a rebound enqueued a second INPUT_PREV and the reader
    //    jumped back two pages on one press.
    assert(classifyButtonRelease(0, false) == BUTTON_RELEASE_IGNORE);

    // 2. Just below the debounce floor is still bounce.
    assert(classifyButtonRelease(BUTTON_DEBOUNCE_MIN_MS - 1, false) == BUTTON_RELEASE_IGNORE);

    // 3. The floor itself counts as a real click (inclusive bound, matching the
    //    >= 30 comparison KEY2 already used).
    assert(classifyButtonRelease(BUTTON_DEBOUNCE_MIN_MS, false) == BUTTON_RELEASE_CLICK);

    // 4. A normal tap.
    assert(classifyButtonRelease(150, false) == BUTTON_RELEASE_CLICK);

    // 5. Last millisecond before long-press territory.
    assert(classifyButtonRelease(BUTTON_LONG_PRESS_MS - 1, false) == BUTTON_RELEASE_CLICK);

    // 6. At the long-press threshold the press-side handler owns the event, so
    //    the release must not also fire a click.
    assert(classifyButtonRelease(BUTTON_LONG_PRESS_MS, false) == BUTTON_RELEASE_IGNORE);
    assert(classifyButtonRelease(5000, false) == BUTTON_RELEASE_IGNORE);

    // 7. Once the long press has fired, the release is always swallowed —
    //    the flag wins over any duration, including a short one.
    assert(classifyButtonRelease(5000, true) == BUTTON_RELEASE_IGNORE);
    assert(classifyButtonRelease(100, true) == BUTTON_RELEASE_IGNORE);
    assert(classifyButtonRelease(0, true) == BUTTON_RELEASE_IGNORE);

    printf("All 7 tests passed.\n");
    return 0;
}
