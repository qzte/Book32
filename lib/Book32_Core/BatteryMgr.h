#pragma once
#include <Arduino.h>

class BatteryMgr {
public:
    static BatteryMgr& getInstance();
    
    void init();
    float getVoltage();
    int getPercentage();
    bool isCharging();
    
private:
    BatteryMgr();
    
    // T-Energy-S3 typical divider: Voltage * 2
    // Some boards use different dividers. We'll start with x2.
    // 18650 Range: 3.0V (0%) to 4.2V (100%)
};
