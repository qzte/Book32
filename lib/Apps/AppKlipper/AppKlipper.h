#ifndef APP_KLIPPER_H
#define APP_KLIPPER_H

#include "BaseApp.h"
#include "../../Book32_Core/InputMgr.h"
#include <vector>

enum KlipperState {
    KLIPPER_SCANNING,
    KLIPPER_LIST,
    KLIPPER_DETAILS
};

struct PrinterInfo {
    String hostname;
    String ip;
    uint16_t port;
    // Status data
    String state;           // "ready", "printing", "paused", "error"
    float extruderTemp;
    float extruderTarget;
    float bedTemp;
    float bedTarget;
    float progress;         // 0-100%
    String filename;        // Current print file
};

class AppKlipper : public App {
public:
    AppKlipper();
    virtual ~AppKlipper();

    // App Interface
    void start() override;
    void stop() override;
    void update() override;
    void draw() override;

    // Icon
    const uint8_t* getIconImage() override;
    const char* getName() override { return "Klipper"; }

    void handleInput(InputAction action);

private:
    KlipperState _state;
    std::vector<PrinterInfo> _printers;
    int _selectedIndex;
    bool _needsRedraw;
    bool _scanning;
    unsigned long _lastScanTime;
    unsigned long _lastUpdateTime;
    
    // Scan progress
    int _scanProgress;  // 0-100%
    int _scannedIPs;
    int _totalIPs;

    // Scanning
    void scanForPrinters();
    void scanSubnet();  // Fallback: scan local subnet for Moonraker port
    bool probeMoonraker(const String& ip, uint16_t port);  // Check if IP is Moonraker
    void drawScanning();

    // List view
    void drawPrinterList();

    // Details view
    void drawPrinterDetails();
    void fetchPrinterStatus(PrinterInfo& printer);

    // Helper to make HTTP request to Moonraker API
    String httpGet(const String& url);
};

#endif
