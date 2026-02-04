#pragma once
#include <Arduino.h>

// Combined battery status to avoid multiple ADC reads
struct BatteryStatus {
    float voltage;
    int percentage;
    bool charging;
};

class BatteryMgr {
public:
    static BatteryMgr& getInstance();

    void init();

    // Preferred: Get all battery info from single cached read
    BatteryStatus getStatus();

    // Legacy methods (still work, but use getStatus() to avoid multiple reads)
    float getVoltage();
    int getPercentage();
    bool isCharging();

private:
    BatteryMgr();

    // Internal: performs actual ADC read and updates cache
    void updateCache();

    // Cached values
    BatteryStatus _cachedStatus;
    unsigned long _lastReadTime;
    static const unsigned long CACHE_DURATION_MS = 5000; // Cache for 5 seconds

    // T-Energy-S3 typical divider: Voltage * 2
    // Some boards use different dividers. We'll start with x2.
    // 18650 Range: 3.0V (0%) to 4.2V (100%)
};
