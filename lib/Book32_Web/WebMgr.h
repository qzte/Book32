#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "Config.h"

class AsyncWebServer; // Forward declaration
class AsyncWebServerRequest; // Forward declaration

class WebMgr {
public:
    static WebMgr& getInstance();
    
    void init();
    void update(); // Handle any main loop needs
    
private:
    WebMgr();
    AsyncWebServer* server; // Pointer instead of object
    
    void setupEndpoints();
    // Handlers need full type in cpp, so keeping signature here implies we need *AsyncWebServerRequest in cpp
    void handleAPIStatus(AsyncWebServerRequest *request);
    void handleAPISettingsGet(AsyncWebServerRequest *request);
    void handleAPISettingsSet(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
    void handleAPICheckUpdate(AsyncWebServerRequest *request);
    void handleAPIUpdateTrigger(AsyncWebServerRequest *request);
};
