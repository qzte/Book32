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

    // Call periodically from main loop to update charge detection and check critical battery
    void update();

    // Preferred: Get all battery info from single cached read
    BatteryStatus getStatus();

    // Legacy methods (still work, but use getStatus() to avoid multiple reads)
    float getVoltage();
    int getPercentage();
    bool isCharging();

    // Check if battery is critically low (should shutdown)
    bool isCriticallyLow();

    // Safely power off the device (deep sleep)
    void shutdownLowBattery();

    // Idle sleep management
    void loadSleepSettings();             // Load from EbookFS
    void resetIdleTimer();                // Call when user interacts
    void enterIdleSleep();                // Display message and sleep

private:
    BatteryMgr();

    // Internal: performs actual ADC read and updates cache
    void updateCache();

    // Cached values
    BatteryStatus _cachedStatus;
    unsigned long _lastReadTime;
    static const unsigned long CACHE_DURATION_MS = 5000; // Cache for 5 seconds

    // Charge detection via voltage trend
    float _voltageHistory[3];          // Store 3 readings over time
    unsigned long _historyTimes[3];    // Timestamps for each reading
    int _historyIndex;                 // Current index in circular buffer
    unsigned long _lastHistoryUpdate;  // Last time we added to history
    static const unsigned long HISTORY_INTERVAL_MS = 30000; // Sample every 30 seconds
    static const float CHARGE_THRESHOLD;  // Voltage increase threshold to detect charging

    // Critical battery threshold
    static const float CRITICAL_VOLTAGE;  // Shutdown below this voltage

    // Idle sleep settings
    int _sleepTimeoutMinutes;          // 0 = disabled
    String _sleepMessage;
    unsigned long _lastActivityTime;   // Last user interaction

    // T-Energy-S3 typical divider: Voltage * 2
    // Some boards use different dividers. We'll start with x2.
    // 18650 Range: 3.0V (0%) to 4.2V (100%)
};
