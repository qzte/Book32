#pragma once
#include <Arduino.h>
#include <time.h>

class TimeMgr {
public:
    static TimeMgr& getInstance();
    
    void init();
    bool syncTime();
    String getFormattedTime(); // HH:MM:SS
    String getFormattedDate(); // YYYY-MM-DD
    
private:
    TimeMgr();
};
