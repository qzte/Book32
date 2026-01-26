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

private:
    DisplayMgr();
    Book32Display display;
};
