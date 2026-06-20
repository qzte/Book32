#pragma once
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include "Config.h"

// Define the display class here to be used across the app
// Using 800x480 BW (Waveshare 7.5 V2)
typedef GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT> Book32Display;

class DisplayMgr {
public:
    static DisplayMgr& getInstance();
    
    void init();
    void update(); // Handles partial updates if needed

    Book32Display& getDisplay() { return display; }

    void clear();
    void fullRefresh();
    void showBootScreen(uint8_t progress, const char* status);

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
};
