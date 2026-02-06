#include "BatteryMgr.h"
#include "../../include/Config.h"
#include "Config.h"
#include <esp_sleep.h>

// Static constants
const float BatteryMgr::CHARGE_THRESHOLD = 0.02f;  // 20mV increase = charging
const float BatteryMgr::CRITICAL_VOLTAGE = 3.0f;   // Shutdown at 3.0V

BatteryMgr::BatteryMgr() : _lastReadTime(0), _historyIndex(0), _lastHistoryUpdate(0) {
    _cachedStatus = {0.0f, 0, false};
    // Initialize history
    for (int i = 0; i < 3; i++) {
        _voltageHistory[i] = 0.0f;
        _historyTimes[i] = 0;
    }
}

BatteryMgr& BatteryMgr::getInstance() {
    static BatteryMgr instance;
    return instance;
}

void BatteryMgr::init() {
    pinMode(PIN_BAT_VOLT, INPUT);
#ifdef PIN_VBAT_SWITCH
    pinMode(PIN_VBAT_SWITCH, OUTPUT);
    digitalWrite(PIN_VBAT_SWITCH, !VBAT_SWITCH_LEVEL); // Keep it off
#endif
    // ADC calibration/attentuation might be needed for S3
    analogSetAttenuation(ADC_11db);

    // Perform initial read to populate cache
    updateCache();

    // Initialize voltage history with current reading
    for (int i = 0; i < 3; i++) {
        _voltageHistory[i] = _cachedStatus.voltage;
        _historyTimes[i] = millis();
    }
    _lastHistoryUpdate = millis();
}

void BatteryMgr::update() {
    unsigned long now = millis();

    // Update voltage history periodically (every 30 seconds)
    if (now - _lastHistoryUpdate >= HISTORY_INTERVAL_MS) {
        // Make sure cache is fresh
        if (now - _lastReadTime >= CACHE_DURATION_MS) {
            updateCache();
        }

        // Add current voltage to history
        _voltageHistory[_historyIndex] = _cachedStatus.voltage;
        _historyTimes[_historyIndex] = now;
        _historyIndex = (_historyIndex + 1) % 3;
        _lastHistoryUpdate = now;

        // Check if voltage is trending upward (charging)
        // Compare oldest reading to current
        int oldestIndex = _historyIndex;  // After increment, this points to oldest
        float oldestVoltage = _voltageHistory[oldestIndex];
        unsigned long oldestTime = _historyTimes[oldestIndex];

        // Only compare if we have enough history (at least 60 seconds)
        if (oldestTime > 0 && (now - oldestTime) >= 60000) {
            float voltageChange = _cachedStatus.voltage - oldestVoltage;

            // If voltage increased by more than threshold, we're charging
            // Also consider it charging if voltage is at/above full charge
            if (voltageChange > CHARGE_THRESHOLD || _cachedStatus.voltage >= 4.18f) {
                _cachedStatus.charging = true;
                Serial.printf("Battery: Charging detected (%.3fV -> %.3fV, delta=%.3fV)\n",
                             oldestVoltage, _cachedStatus.voltage, voltageChange);
            } else {
                _cachedStatus.charging = false;
            }
        }
    }

    // Check for critical low battery
    if (isCriticallyLow()) {
        Serial.println("CRITICAL: Battery voltage too low! Shutting down...");
        shutdownLowBattery();
    }
}

void BatteryMgr::updateCache() {
#ifdef PIN_VBAT_SWITCH
    digitalWrite(PIN_VBAT_SWITCH, VBAT_SWITCH_LEVEL); // Turn on measurement
    delay(5); // Wait for stabilization
#endif

    // Read ADC - average 10 samples
    uint32_t raw = 0;
    for(int i = 0; i < 10; i++) {
        raw += analogRead(PIN_BAT_VOLT);
        delay(1);
    }
    raw /= 10;

#ifdef PIN_VBAT_SWITCH
    digitalWrite(PIN_VBAT_SWITCH, !VBAT_SWITCH_LEVEL); // Turn off to save power
#endif

    // Convert to voltage (divider ratio x2)
    float voltage = (raw / 4095.0f) * 3.3f * 2.0f;

    // Calculate percentage (LiPo: 3.0V = 0%, 4.2V = 100%)
    int percentage = 0;
    if (voltage >= 4.2f) percentage = 100;
    else if (voltage <= 3.0f) percentage = 0;
    else percentage = (int)((voltage - 3.0f) / (4.2f - 3.0f) * 100.0f);

    // Preserve charging state from trend analysis (don't overwrite here)
    bool currentCharging = _cachedStatus.charging;

    // Update cache (keep charging state from trend analysis)
    _cachedStatus = {voltage, percentage, currentCharging};
    _lastReadTime = millis();
}

bool BatteryMgr::isCriticallyLow() {
    // Make sure we have a fresh reading
    if (millis() - _lastReadTime >= CACHE_DURATION_MS) {
        updateCache();
    }
    return _cachedStatus.voltage <= CRITICAL_VOLTAGE && !_cachedStatus.charging;
}

void BatteryMgr::shutdownLowBattery() {
    Serial.println("Battery critically low - entering deep sleep");
    Serial.printf("Voltage: %.2fV\n", _cachedStatus.voltage);
    Serial.flush();

    // Small delay to let serial finish
    delay(100);

    // Enter deep sleep indefinitely (will wake on reset/power)
    // This is the safest way to "power off" on ESP32
    esp_deep_sleep_start();
}

BatteryStatus BatteryMgr::getStatus() {
    // Refresh cache if expired
    if (millis() - _lastReadTime >= CACHE_DURATION_MS) {
        updateCache();
    }
    return _cachedStatus;
}

float BatteryMgr::getVoltage() {
    return getStatus().voltage;
}

int BatteryMgr::getPercentage() {
    return getStatus().percentage;
}

bool BatteryMgr::isCharging() {
    return getStatus().charging;
}
