#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "Config.h"

class AsyncWebServer; // Forward declaration
class AsyncWebServerRequest; // Forward declaration

class WebMgr {
public:
    static WebMgr& getInstance();
    
    void mountFilesystems();  // Call early, before WiFi
    void init();              // Call after WiFi connected
    void stop();              // Stop web services before powering WiFi down
    void update(); // Handle any main loop needs (including pending OTA)
    bool isInitialized() const { return _initialized; }

    // Per-device credential derived from the WiFi MAC. Since v1.9.0 this is
    // only the SoftAP WPA2 passphrase — the HTTP API no longer asks for a
    // login. See lib/Book32_Core/DeviceCred.h for the threat model.
    static const char* devicePassword();
    
    volatile bool _otaPending = false;  // Flag to trigger OTA from main loop

    // Deferred display changes: set by async web handlers, applied from the main
    // loop (in update()) so drawing never happens on the async server task.
    volatile int _pendingRotation = 0;        // 0 = none, else 1 or 3
    volatile int _pendingReaderFontSize = 0;  // 0 = none, else 10/12/14/16/18/20
    volatile int _pendingReaderFontFamily = -1; // -1 = none, else ReaderFontFamily (0-4)
    volatile int _pendingAppSwitch = -1;        // -1 = none, else index in AppMgr::getApps()

private:
    WebMgr();
    AsyncWebServer* server; // Pointer instead of object
    bool _initialized = false;
    bool _endpointsConfigured = false;

    // Registo das rotas HTTP, por domínio. setupEndpoints() não faz mais nada
    // do que chamá-las pela ordem abaixo; registerStaticRoutes() tem de ficar
    // em último, porque o seu handler de "/" apanha tudo o que sobra. Ver o
    // comentário em WebMgr.cpp.
    void setupEndpoints();
    void registerSystemRoutes();
    void registerBookRoutes();
    void registerUpdateRoutes();
    void registerSettingsRoutes();
    void registerReaderRoutes();
    void registerLibraryStateRoutes();
    void registerWifiRoutes();
    void registerStaticRoutes();
};
