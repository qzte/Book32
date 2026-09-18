#include "SettingsStore.h"
#include "Book32FS.h"
#include "JsonFileStore.h"
#include <ArduinoJson.h>

static const char* READER_CONFIG_PATH = "/reader_config.json";
static const char* DISPLAY_CONFIG_PATH = "/display_config.json";
static const char* SLEEP_CONFIG_PATH = "/sleep_config.json";

SettingsStore& SettingsStore::getInstance() {
    static SettingsStore instance;
    return instance;
}

// Todos os métodos abaixo tomam o mesmo mutex recursivo, directamente ou
// através de uma Transaction que já o detém.
SettingsStore::Transaction::Transaction() {
    SettingsStore::getInstance()._mutex.lock();
}

SettingsStore::Transaction::~Transaction() {
    SettingsStore::getInstance()._mutex.unlock();
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
    if (family < 0 || family > 5) return 0;
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
    Book32Guard guard(_mutex);
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
        DynamicJsonDocument doc(384);
        if (!deserializeJson(doc, file)) {
            s.refreshFrequency = clampRefreshFrequency(doc["refreshFrequency"] | 10);
            s.fontSize = clampFontSize(doc["fontSize"] | 9);
            s.fontFamily = clampFontFamily(doc["fontFamily"] | 0);
            s.showChapter = doc["showChapter"] | false;
            s.showPageNumber = doc["showPageNumber"] | true;
            s.showReadingPercentage = doc["showReadingPercentage"] | false;
        }
        file.close();
    }

    return s;
}

DisplaySettings SettingsStore::loadDisplay() {
    Book32Guard guard(_mutex);
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
    Book32Guard guard(_mutex);
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
    Book32Guard guard(_mutex);
    DynamicJsonDocument doc(384);
    doc["refreshFrequency"] = clampRefreshFrequency(s.refreshFrequency);
    doc["fontSize"] = clampFontSize(s.fontSize);
    doc["fontFamily"] = clampFontFamily(s.fontFamily);
    doc["showChapter"] = s.showChapter;
    doc["showPageNumber"] = s.showPageNumber;
    doc["showReadingPercentage"] = s.showReadingPercentage;

    // Escrita atómica (.tmp + rename, ver JsonFileStore.h): estas definições
    // são pequenas, mas FILE_WRITE trunca o ficheiro bom antes de escrever, e
    // uma falha de energia a meio deixava o utilizador com as definições
    // repostas de origem.
    if (!writeJsonAtomic(EbookFS, READER_CONFIG_PATH, doc, "SettingsStore(reader)")) return false;

    Serial.printf("SettingsStore: saved reader refreshFrequency=%d fontSize=%d fontFamily=%d showChapter=%d "
                  "showPageNumber=%d showReadingPercentage=%d\n",
                  doc["refreshFrequency"].as<int>(), doc["fontSize"].as<int>(), doc["fontFamily"].as<int>(),
                  doc["showChapter"].as<bool>(), doc["showPageNumber"].as<bool>(),
                  doc["showReadingPercentage"].as<bool>());
    return true;
}

bool SettingsStore::saveDisplay(const DisplaySettings& s) {
    Book32Guard guard(_mutex);
    DynamicJsonDocument doc(128);
    doc["rotation"] = clampRotation(s.rotation);

    // Escrita atómica (.tmp + rename, ver JsonFileStore.h): estas definições
    // são pequenas, mas FILE_WRITE trunca o ficheiro bom antes de escrever, e
    // uma falha de energia a meio deixava o utilizador com as definições
    // repostas de origem.
    if (!writeJsonAtomic(EbookFS, DISPLAY_CONFIG_PATH, doc, "SettingsStore(display)")) return false;

    Serial.printf("SettingsStore: saved display rotation=%d\n", doc["rotation"].as<int>());
    return true;
}

bool SettingsStore::saveSleep(const SleepSettings& s) {
    Book32Guard guard(_mutex);
    DynamicJsonDocument doc(512);
    doc["sleepTimeout"] = clampSleepTimeout(s.timeout);
    doc["sleepMessage"] = s.message;

    // Escrita atómica (.tmp + rename, ver JsonFileStore.h): estas definições
    // são pequenas, mas FILE_WRITE trunca o ficheiro bom antes de escrever, e
    // uma falha de energia a meio deixava o utilizador com as definições
    // repostas de origem.
    if (!writeJsonAtomic(EbookFS, SLEEP_CONFIG_PATH, doc, "SettingsStore(sleep)")) return false;

    Serial.printf("SettingsStore: saved sleep timeout=%d message=%s\n",
                  doc["sleepTimeout"].as<int>(), doc["sleepMessage"].as<String>().c_str());
    return true;
}
