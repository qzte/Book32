#include "DisplayMgr.h"
#include "../../include/Config.h"

// Constructor with Pin mapping
// GxEPD2_420(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
DisplayMgr::DisplayMgr() : display(GxEPD2_750_T7(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)) {
}

DisplayMgr& DisplayMgr::getInstance() {
    static DisplayMgr instance;
    return instance;
}

void DisplayMgr::init() {
    // For ESP32-S3 we must initialize SPI with custom pins before display.init
    SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS); 
    
    // GxEPD2 initialization
    // 115200: baud rate for serial output
    // true: initial reset
    // 10: reset duration (ms)
    // false: serial feedback disabled
    display.init(115200, true, 10, false); 
    
    display.setRotation(1); // Portrait mode for 480x800
    display.setTextColor(GxEPD_BLACK);
    display.setFont(NULL); // Default font
}

void DisplayMgr::clear() {
    display.clearScreen();
}

void DisplayMgr::fullRefresh() {
    display.refresh(true); // Full update
}

void DisplayMgr::update() {
    // E-ink usually updates on demand, not every loop
}
