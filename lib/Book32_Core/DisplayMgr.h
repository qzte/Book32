#pragma once
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <atomic>
#include "Config.h"
#include "GxEPD2_750_T7_Fast.h"

// Define the display class here to be used across the app
// Using 800x480 BW (Waveshare 7.5 V2). GxEPD2_750_T7_Fast is GxEPD2's
// GxEPD2_750_T7 with shorter waveforms (page turn ~1.15s instead of ~1.8s,
// full refresh ~1.6s instead of ~3.9s); see GxEPD2_750_T7_Fast.h.
typedef GxEPD2_BW<GxEPD2_750_T7_Fast, GxEPD2_750_T7_Fast::HEIGHT> Book32Display;

class DisplayMgr {
public:
    static DisplayMgr& getInstance();
    
    void init();
    // Runs a full refresh queued by requestFullRefresh(), if any, and powers
    // the panel down once input has been quiet for PANEL_POWER_HOLD_MS.
    void update();

    Book32Display& getDisplay() { return display; }

    void clear();
    void fullRefresh();
    // Queues a fullRefresh() for the next update() instead of doing it here:
    // refresh(false) is a ~2s blocking e-ink cycle, and callers of this (KEY2
    // click, from InputMgr's action-dequeue loop) must not stall waiting on
    // it. update() runs it after AppMgr::draw() has already repainted the
    // app's new content, so the hard refresh clears ghosting around what the
    // user asked to see, not the stale frame from before the click.
    void requestFullRefresh();
    // Boot frames are drawn by a worker task while setup() carries on: each
    // one blocks for a whole e-ink refresh, and setup() used to wait for all
    // six. Only the newest status is kept — one superseded while the panel is
    // busy is skipped, not queued.
    void showBootScreen(uint8_t progress, const char* status);
    // Waits for the last boot frame to reach the panel and hands the display
    // back to the caller. Must run before the first app draw.
    void finishBootScreen();

    // Code that draws from a task other than loop() (the OTA progress
    // screen) holds this so update() does not power the panel down — a
    // SPI command from loop() — in the middle of one of its frames.
    class PowerHold {
      public:
        PowerHold();
        ~PowerHold();
        PowerHold(const PowerHold&) = delete;
        PowerHold& operator=(const PowerHold&) = delete;
    };

    // Display orientation. Only the two portrait orientations are supported so
    // that every screen layout (480x800) stays valid: 3 = button on the left
    // (default), 1 = rotated 180 (button on the right). Applies OS-wide.
    void setRotation(int rotation);
    int getRotation() const { return _rotation; }
    void loadDisplaySettings();  // Reads /display_config.json (call after FS mount)

private:
    DisplayMgr();
    Book32Display display;
    bool _bootScreenActive = false;
    int _rotation = 3;  // Default: portrait, button on the left
    bool _pendingFullRefresh = false;
    std::atomic<int> _powerHolds{0};

    void renderBootScreen(uint8_t progress, const char* status);
    static void bootScreenTask(void* parameter);
    void waitForBootScreenIdle();
    SemaphoreHandle_t _bootLock = nullptr;
    volatile bool _bootTaskRunning = false;
    bool _bootPending = false;
    bool _bootFinished = false;
    uint8_t _bootProgress = 0;
    char _bootStatus[40] = "";
};
