#pragma once
// Book32 v1.8.1 — shared button release classification.
//
// KEY1 and KEY2 are both read with a bare digitalRead() from InputMgr's polling
// task, so OneButton's tick() (and therefore its setDebounceMs) never runs on
// them. Debounce has to happen here instead.
//
// KEY2 had a >= 30ms floor for exactly this reason; KEY1 did not, so a rebound
// on release enqueued a second INPUT_PREV and the reader jumped back two pages
// on a single press. Both buttons now share this one classifier so the
// asymmetry cannot come back.
//
// Pure header, no Arduino dependencies — host-testable
// (tools/tests/test_button_press.cpp).

enum ButtonRelease {
    BUTTON_RELEASE_IGNORE,  // Contact bounce, or the long press already fired
    BUTTON_RELEASE_CLICK    // A real short click
};

// Anything shorter than this is contact bounce, not a press.
constexpr unsigned long BUTTON_DEBOUNCE_MIN_MS = 30;

// At or beyond this, the press-side handler owns the event (long press).
constexpr unsigned long BUTTON_LONG_PRESS_MS = 400;

// pressDuration: millis() elapsed between press and release.
// longPressSent: whether the press-side handler already fired the long press.
inline ButtonRelease classifyButtonRelease(unsigned long pressDuration, bool longPressSent) {
    if (longPressSent) return BUTTON_RELEASE_IGNORE;
    if (pressDuration < BUTTON_DEBOUNCE_MIN_MS) return BUTTON_RELEASE_IGNORE;
    if (pressDuration >= BUTTON_LONG_PRESS_MS) return BUTTON_RELEASE_IGNORE;
    return BUTTON_RELEASE_CLICK;
}
