#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H

#include <Arduino.h>

// =============================================================================
// SettingsStore
// =============================================================================
// Single source of truth for the persisted configuration files. Both the web
// layer (WebMgr) and the on-device settings menu (AppSettings) go through this
// class so that defaults and clamping rules live in exactly one place.
//
// Before this existed, WebMgr read and wrote the JSON files inline in each HTTP
// handler with the defaults hardcoded per lambda. Adding a second consumer that
// way would have guaranteed drift the first time a default changed.
//
// Storage lives on EbookFS so settings survive OTA firmware updates (the
// SystemFS partition is rewritten by `uploadfs`).
// =============================================================================

struct ReaderSettings {
    int refreshFrequency = 10;  // Full e-ink refresh every N page turns
    int fontSize = 9;           // Reading body size in points: 9, 12 or 18
    int fontFamily = 0;         // See ReaderFontFamily: 0..4
};

struct DisplaySettings {
    int rotation = 3;  // 3 = button on the left (default), 1 = rotated 180
};

struct SleepSettings {
    int timeout = 0;  // Idle sleep timeout in minutes; 0 = disabled
    String message = "Press button to wake";
};

class SettingsStore {
public:
    static SettingsStore& getInstance();

    // Load never fails: a missing file or malformed JSON yields defaults.
    ReaderSettings loadReader();
    DisplaySettings loadDisplay();
    SleepSettings loadSleep();

    // Save applies clamping first, then writes. Returns false only on I/O
    // failure so callers can keep the user's edits instead of discarding them.
    bool saveReader(const ReaderSettings& s);
    bool saveDisplay(const DisplaySettings& s);
    bool saveSleep(const SleepSettings& s);

    // Clamping helpers, exposed so the UI can snap a value to the next legal
    // one while cycling rather than duplicating the allowed sets.
    static int clampFontSize(int pt);       // -> 9, 12 or 18
    static int clampFontFamily(int family); // -> 0..4
    static int clampRotation(int rotation); // -> 1 or 3
    static int clampRefreshFrequency(int n);
    static int clampSleepTimeout(int minutes);

private:
    SettingsStore() {}
};

#endif
