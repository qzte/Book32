#include "SettingsStore.h"
#include "Book32FS.h"
#include <ArduinoJson.h>

static const char* READER_CONFIG_PATH = "/reader_config.json";
static const char* DISPLAY_CONFIG_PATH = "/display_config.json";
static const char* SLEEP_CONFIG_PATH = "/sleep_config.json";

SettingsStore& SettingsStore::getInstance() {
    static SettingsStore instance;
    return instance;
}

// --- Clamping ---------------------------------------------------------------
// The reading fonts are only generated at three sizes, so anything else would
// fall back to a missing glyph set. Snap to the nearest supported size.
int SettingsStore::clampFontSize(int pt) {
    if (pt >= 18) return 18;
    if (pt >= 12) return 12;
    return 9;
}

int SettingsStore::clampFontFamily(int family) {
    if (family < 0 || family > 4) return 0;
    return family;
}

// Only the two portrait orientations keep the 480x800 layouts valid.
int SettingsStore::clampRotation(int rotation) {
    return (rotation == 1) ? 1 : 3;
}

int SettingsStore::clampRefreshFrequency(int n) {
    if (n < 1) return 1;
    if (n > 100) return 100;
    return n;
}

int SettingsStore::clampSleepTimeout(int minutes) {
    if (minutes < 0) return 0;
    if (minutes > 240) return 240;
    return minutes;
}

// --- Load -------------------------------------------------------------------
ReaderSettings SettingsStore::loadReader() {
    ReaderSettings s;

    // EbookFS is primary. SystemFS is a legacy fallback kept so devices
    // upgraded from older firmware don't silently lose their settings.
    File file;
    if (EbookFS.exists(READER_CONFIG_PATH)) {
        file = EbookFS.open(READER_CONFIG_PATH, "r");
    } else if (SystemFS.exists(READER_CONFIG_PATH)) {
        file = SystemFS.open(READER_CONFIG_PATH, "r");
    }

    if (file) {
        DynamicJsonDocument doc(256);
        if (!deserializeJson(doc, file)) {
            s.refreshFrequency = clampRefreshFrequency(doc["refreshFrequency"] | 10);
            s.fontSize = clampFontSize(doc["fontSize"] | 9);
            s.fontFamily = clampFontFamily(doc["fontFamily"] | 0);
        }
        file.close();
    }

    return s;
}

DisplaySettings SettingsStore::loadDisplay() {
    DisplaySettings s;

    if (EbookFS.exists(DISPLAY_CONFIG_PATH)) {
        File file = EbookFS.open(DISPLAY_CONFIG_PATH, "r");
        if (file) {
            DynamicJsonDocument doc(128);
            if (!deserializeJson(doc, file)) {
                s.rotation = clampRotation(doc["rotation"] | 3);
            }
            file.close();
        }
    }

    return s;
}

SleepSettings SettingsStore::loadSleep() {
    SleepSettings s;

    if (EbookFS.exists(SLEEP_CONFIG_PATH)) {
        File file = EbookFS.open(SLEEP_CONFIG_PATH, "r");
        if (file) {
            DynamicJsonDocument doc(512);
            if (!deserializeJson(doc, file)) {
                s.timeout = clampSleepTimeout(doc["sleepTimeout"] | 0);
                s.message = doc["sleepMessage"] | "Press button to wake";
            }
            file.close();
        }
    }

    return s;
}

// --- Save -------------------------------------------------------------------
bool SettingsStore::saveReader(const ReaderSettings& s) {
    DynamicJsonDocument doc(256);
    doc["refreshFrequency"] = clampRefreshFrequency(s.refreshFrequency);
    doc["fontSize"] = clampFontSize(s.fontSize);
    doc["fontFamily"] = clampFontFamily(s.fontFamily);

    File file = EbookFS.open(READER_CONFIG_PATH, FILE_WRITE);
    if (!file) {
        Serial.println("SettingsStore: failed to open reader_config.json for write");
        return false;
    }
    serializeJson(doc, file);
    file.close();

    Serial.printf("SettingsStore: saved reader refreshFrequency=%d fontSize=%d fontFamily=%d\n",
                  doc["refreshFrequency"].as<int>(), doc["fontSize"].as<int>(),
                  doc["fontFamily"].as<int>());
    return true;
}

bool SettingsStore::saveDisplay(const DisplaySettings& s) {
    DynamicJsonDocument doc(128);
    doc["rotation"] = clampRotation(s.rotation);

    File file = EbookFS.open(DISPLAY_CONFIG_PATH, FILE_WRITE);
    if (!file) {
        Serial.println("SettingsStore: failed to open display_config.json for write");
        return false;
    }
    serializeJson(doc, file);
    file.close();

    Serial.printf("SettingsStore: saved display rotation=%d\n", doc["rotation"].as<int>());
    return true;
}

bool SettingsStore::saveSleep(const SleepSettings& s) {
    DynamicJsonDocument doc(512);
    doc["sleepTimeout"] = clampSleepTimeout(s.timeout);
    doc["sleepMessage"] = s.message;

    File file = EbookFS.open(SLEEP_CONFIG_PATH, FILE_WRITE);
    if (!file) {
        Serial.println("SettingsStore: failed to open sleep_config.json for write");
        return false;
    }
    serializeJson(doc, file);
    file.close();

    Serial.printf("SettingsStore: saved sleep timeout=%d message=%s\n",
                  doc["sleepTimeout"].as<int>(), doc["sleepMessage"].as<String>().c_str());
    return true;
}
