#include "BatteryMgr.h"
#include "../../include/Config.h"
#include "Config.h"

BatteryMgr::BatteryMgr() : _lastReadTime(0) {
    _cachedStatus = {0.0f, 0, false};
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

    // Check if charging (voltage > 4.22V indicates external power)
    bool charging = (voltage > 4.22f);

    // Update cache
    _cachedStatus = {voltage, percentage, charging};
    _lastReadTime = millis();
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
