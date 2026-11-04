#include "DisplayMgr.h"
#include "../../include/Config.h"
#include "Book32FS.h"
#include "BatteryMgr.h"
#include <ArduinoJson.h>
#include <Preferences.h>
// Local FreeSans with Latin-1 Supplement (0x20-0xFF).
#include "Fonts/FreeSans.h"

static void drawCenteredText(Book32Display& display, const char* text, const GFXfont* font, int16_t baseline, uint16_t color) {
    int16_t x1, y1;
    uint16_t w, h;
    display.setFont(font);
    display.setTextColor(color);
    display.setTextSize(1);
    display.getTextBounds(text, 0, baseline, &x1, &y1, &w, &h);
    display.setCursor((display.width() - w) / 2 - x1, baseline);
    display.print(text);
}

static void drawBootProgress(Book32Display& display, uint8_t progress, const char* status) {
    int16_t screenW = display.width();
    const int16_t barW = 320;
    const int16_t barH = 28;
    const int16_t barX = (screenW - barW) / 2;
    const int16_t barY = 430;
    const int16_t fillW = ((barW - 8) * progress) / 100;

    char percentText[8];
    snprintf(percentText, sizeof(percentText), "%u%%", (unsigned int)progress);

    display.fillRect(barX - 20, barY - 8, barW + 40, 150, GxEPD_WHITE);
    display.drawRoundRect(barX, barY, barW, barH, 7, GxEPD_BLACK);
    display.drawRoundRect(barX + 2, barY + 2, barW - 4, barH - 4, 5, GxEPD_BLACK);
    if (fillW > 0) {
        display.fillRoundRect(barX + 4, barY + 4, fillW, barH - 8, 3, GxEPD_BLACK);
    }

    drawCenteredText(display, percentText, &FreeSansBold18pt8b, barY + 76, GxEPD_BLACK);
    drawCenteredText(display, status, &FreeSans12pt8b, barY + 116, GxEPD_BLACK);
}

// After a button press the panel's high-voltage booster stays armed for this
// long, so quick page turns skip the ~150ms power-up; then update() turns it
// off. GxEPD2 only powers the panel down after a full refresh — partial
// updates left the booster on indefinitely, which slowly darkens the panel
// edge (seen by rolohaun/Book32 on the same kit, InkDeck 1.3.3).
static const unsigned long PANEL_POWER_HOLD_MS = 10000;

// The orientation is mirrored into NVS so the very first boot frame is drawn
// the right way up; /display_config.json (on ebook storage) stays the source
// of truth and is re-applied by loadDisplaySettings() once storage mounts.
static const char* ROTATION_PREFS_NAMESPACE = "display";
static const char* ROTATION_PREFS_KEY = "rotation";

static int loadCachedRotation() {
    Preferences prefs;
    int rotation = 3;
    if (prefs.begin(ROTATION_PREFS_NAMESPACE, true)) {
        rotation = prefs.getInt(ROTATION_PREFS_KEY, 3);
        prefs.end();
    }
    return (rotation == 1 || rotation == 3) ? rotation : 3;
}

static void saveCachedRotation(int rotation) {
    Preferences prefs;
    if (prefs.begin(ROTATION_PREFS_NAMESPACE, false)) {
        prefs.putInt(ROTATION_PREFS_KEY, rotation);
        prefs.end();
    }
}

// Constructor with Pin mapping
// GxEPD2_420(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
DisplayMgr::DisplayMgr() : display(GxEPD2_750_T7_Fast(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)) {}

DisplayMgr& DisplayMgr::getInstance() {
    static DisplayMgr instance;
    return instance;
}

void DisplayMgr::init() {
    // For ESP32-S3 we must initialize SPI with custom pins before display.init
    SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
    // GxEPD2 defaults to 4MHz and sends each 48KB frame byte by byte. The
    // GD7965 accepts writes well above 10MHz; stay conservative for the DIY
    // kit's wiring.
    display.epd2.selectSPI(SPI, SPISettings(10000000, MSBFIRST, SPI_MODE0));

    _rotation = loadCachedRotation();
    display.init(115200, true, 10, false);

    display.setRotation(_rotation); // Portrait mode (480x800); _rotation = 1 or 3
    display.setTextColor(GxEPD_BLACK);
    display.setFont(NULL);

    Serial.printf("Display initialized: %dx%d (rotation %d)\n", display.width(), display.height(), _rotation);
}

void DisplayMgr::setRotation(int rotation) {
    // Constrain to the two portrait orientations so layouts never break.
    if (rotation != 1 && rotation != 3) rotation = 3;
    bool changed = (rotation != _rotation);
    // Never rotate the canvas underneath a boot frame that is being drawn.
    if (changed) waitForBootScreenIdle();
    _rotation = rotation;
    if (changed) saveCachedRotation(rotation);
    display.setRotation(_rotation);
    // Only invalidate the boot screen when the orientation actually changes, so
    // a no-op load at boot doesn't trigger a full refresh mid-sequence. Live
    // (post-boot) rotation changes repaint via the active app's forceRedraw().
    if (changed) _bootScreenActive = false;
    Serial.printf("Display rotation set to %d (changed=%d)\n", _rotation, changed);
}

void DisplayMgr::loadDisplaySettings() {
    int rotation = 3;  // Default: button on the left
    if (EbookFS.exists("/display_config.json")) {
        File file = EbookFS.open("/display_config.json", "r");
        if (file) {
            DynamicJsonDocument doc(256);
            if (!deserializeJson(doc, file)) {
                rotation = doc["rotation"] | 3;
            }
            file.close();
        }
    }
    setRotation(rotation);
    Serial.printf("Loaded display settings: rotation=%d\n", _rotation);
}

void DisplayMgr::clear() {
    display.clearScreen();
}

void DisplayMgr::fullRefresh() {
    // GxEPD2's refresh(bool partial_update_mode = false): false is the full
    // update this function is named for, true is partial. This was inverted
    // — refresh(true) — which made fullRefresh() silently do a partial
    // update instead, defeating the point of a manual ghosting-clear.
    display.refresh(false); // Full update
}

void DisplayMgr::requestFullRefresh() {
    _pendingFullRefresh = true;
}

void DisplayMgr::showBootScreen(uint8_t progress, const char* status) {
    if (progress > 100) progress = 100;
    if (!status) status = "";

    if (!_bootFinished) {
        if (!_bootLock) _bootLock = xSemaphoreCreateMutex();
        if (_bootLock) {
            xSemaphoreTake(_bootLock, portMAX_DELAY);
            _bootProgress = progress;
            strlcpy(_bootStatus, status, sizeof(_bootStatus));
            _bootPending = true;
            bool startWorker = !_bootTaskRunning;
            if (startWorker) _bootTaskRunning = true;
            xSemaphoreGive(_bootLock);

            if (!startWorker) return; // The running worker picks it up
            if (xTaskCreatePinnedToCore(bootScreenTask, "BootScreen", 6144, this, 1, nullptr, 1) == pdPASS) {
                return;
            }
            // No worker: fall through and draw this status synchronously.
            xSemaphoreTake(_bootLock, portMAX_DELAY);
            _bootTaskRunning = false;
            _bootPending = false;
            xSemaphoreGive(_bootLock);
        }
    }
    renderBootScreen(progress, status);
}

void DisplayMgr::bootScreenTask(void* parameter) {
    DisplayMgr* self = static_cast<DisplayMgr*>(parameter);
    for (;;) {
        xSemaphoreTake(self->_bootLock, portMAX_DELAY);
        if (!self->_bootPending) {
            self->_bootTaskRunning = false;
            xSemaphoreGive(self->_bootLock);
            break;
        }
        uint8_t progress = self->_bootProgress;
        char status[sizeof(self->_bootStatus)];
        strlcpy(status, self->_bootStatus, sizeof(status));
        self->_bootPending = false;
        xSemaphoreGive(self->_bootLock);

        self->renderBootScreen(progress, status);
    }
    vTaskDelete(nullptr);
}

void DisplayMgr::waitForBootScreenIdle() {
    while (_bootTaskRunning)
        delay(5);
}

void DisplayMgr::finishBootScreen() {
    waitForBootScreenIdle();
    _bootFinished = true;
}

void DisplayMgr::renderBootScreen(uint8_t progress, const char* status) {
    int16_t screenW = display.width();
    int16_t screenH = display.height();

    bool drawFullFrame = !_bootScreenActive;
    if (drawFullFrame) {
        display.setFullWindow();
        _bootScreenActive = true;
    } else {
        display.setPartialWindow(60, 410, screenW - 120, 180);
    }

    const int16_t frameInset = 18;
    const int16_t logoY = 185;
    const int16_t titleY = 330;

    display.firstPage();
    do {
        display.setTextColor(GxEPD_BLACK);
        display.setTextSize(1);

        if (drawFullFrame) {
            display.fillScreen(GxEPD_WHITE);
            display.drawRect(frameInset, frameInset, screenW - (frameInset * 2), screenH - (frameInset * 2), GxEPD_BLACK);
            display.drawFastHLine(54, 64, screenW - 108, GxEPD_BLACK);
            display.drawFastHLine(54, screenH - 64, screenW - 108, GxEPD_BLACK);

            int16_t logoX = (screenW - 116) / 2;
            display.drawRoundRect(logoX, logoY, 54, 70, 5, GxEPD_BLACK);
            display.drawRoundRect(logoX + 62, logoY, 54, 70, 5, GxEPD_BLACK);
            display.fillRect(logoX + 55, logoY + 6, 6, 58, GxEPD_BLACK);
            display.drawFastHLine(logoX + 10, logoY + 18, 34, GxEPD_BLACK);
            display.drawFastHLine(logoX + 10, logoY + 32, 34, GxEPD_BLACK);
            display.drawFastHLine(logoX + 10, logoY + 46, 28, GxEPD_BLACK);
            display.drawFastHLine(logoX + 72, logoY + 18, 34, GxEPD_BLACK);
            display.drawFastHLine(logoX + 72, logoY + 32, 34, GxEPD_BLACK);
            display.drawFastHLine(logoX + 72, logoY + 46, 28, GxEPD_BLACK);

            drawCenteredText(display, DEVICE_NAME, &FreeSansBold24pt8b, titleY, GxEPD_BLACK);
            drawCenteredText(display, "Starting Book32 OS", &FreeSans12pt8b, titleY + 42, GxEPD_BLACK);

            char versionText[20];
            snprintf(versionText, sizeof(versionText), "v%s", SYSTEM_VERSION);
            drawCenteredText(display, versionText, &FreeSans9pt8b, screenH - 96, GxEPD_BLACK);
        }

        drawBootProgress(display, progress, status);
    } while (display.nextPage());
}

void DisplayMgr::update() {
    // E-ink usually updates on demand, not every loop — the one exception is
    // a full refresh queued by requestFullRefresh(), run here (after
    // AppMgr::draw() in loop()) so it's the freshly drawn frame that gets
    // the ghosting-clearing pass, not whatever was on screen before.
    if (_pendingFullRefresh) {
        _pendingFullRefresh = false;
        fullRefresh();
    }

    // powerOff() is a no-op when the panel is already off; the next partial
    // update re-arms the booster itself.
    if (_powerHolds == 0 &&
        millis() - BatteryMgr::getInstance().getLastActivityTime() >= PANEL_POWER_HOLD_MS) {
        display.powerOff();
    }
}

DisplayMgr::PowerHold::PowerHold() {
    DisplayMgr::getInstance()._powerHolds++;
}

DisplayMgr::PowerHold::~PowerHold() {
    DisplayMgr::getInstance()._powerHolds--;
}
