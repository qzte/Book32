#include "TimeMgr.h"
#include "../../include/Config.h"

TimeMgr::TimeMgr() {}

TimeMgr& TimeMgr::getInstance() {
    static TimeMgr instance;
    return instance;
}

void TimeMgr::init() {
    // Configure Time
    configTzTime(TIMEZONE, NTP_SERVER);
    
    Serial.println("TimeMgr: Syncing time...");
    // Wait for sync (optional, or let it happen in background)
    // We force a quick check to ensure we have valid time for logs
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo, 2000)){ // 2 sec timeout
        Serial.println("TimeMgr: Failed to obtain time immediately");
    } else {
        Serial.println("TimeMgr: " + getFormattedTime());
    }
}

String TimeMgr::getFormattedTime() {
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
        return "--:--:--";
    }
    char timeStringBuff[10];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M:%S", &timeinfo);
    return String(timeStringBuff);
}

String TimeMgr::getFormattedDate() {
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
        return "YYYY-MM-DD";
    }
    char timeStringBuff[15];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d", &timeinfo);
    return String(timeStringBuff);
}
