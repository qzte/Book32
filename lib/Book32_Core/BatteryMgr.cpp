#include "BatteryMgr.h"
#include "../../include/Config.h"
#include "Config.h"
#include <esp_sleep.h>
#include "Book32FS.h"
#include "DisplayMgr.h"
#include <ArduinoJson.h>
#include <Fonts/FreeSans18pt7b.h>

// Static constants
const float BatteryMgr::CHARGE_THRESHOLD = 0.02f;  // 20mV increase = charging
const float BatteryMgr::CRITICAL_VOLTAGE = 3.0f;   // Shutdown at 3.0V
const float BatteryMgr::HIGH_VOLTAGE_THRESHOLD = 4.1f;  // Assume charging if voltage >= this

BatteryMgr::BatteryMgr() : _lastReadTime(0), _historyIndex(0), _lastHistoryUpdate(0),
                           _previousVoltage(0.0f), _sleepTimeoutMinutes(5),
                           _sleepMessage("Press button to wake"), _lastActivityTime(0) {
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

    // Initialize voltage history and previous voltage with current reading
    _previousVoltage = _cachedStatus.voltage;
    for (int i = 0; i < 3; i++) {
        _voltageHistory[i] = _cachedStatus.voltage;
        _historyTimes[i] = millis();
    }
    _lastHistoryUpdate = millis();

    // Check for immediate charging indicators on startup
    if (_cachedStatus.voltage >= HIGH_VOLTAGE_THRESHOLD) {
        _cachedStatus.charging = true;
        Serial.printf("Battery: High voltage (%.2fV) - assuming charging/plugged in\n", _cachedStatus.voltage);
    }

    // Load sleep settings from EbookFS
    loadSleepSettings();

    // Initialize activity timer
    _lastActivityTime = millis();
}

void BatteryMgr::update() {
    unsigned long now = millis();

    // Quick check: if voltage is high, assume charging (battery can't stay this high unplugged)
    // This provides immediate detection without waiting for trend analysis
    if (_cachedStatus.voltage >= HIGH_VOLTAGE_THRESHOLD) {
        if (!_cachedStatus.charging) {
            _cachedStatus.charging = true;
            Serial.printf("Battery: High voltage (%.2fV >= %.2fV) - charging detected\n",
                         _cachedStatus.voltage, HIGH_VOLTAGE_THRESHOLD);
        }
    }

    // Update voltage history periodically for trend analysis
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

        // Compare with oldest reading (at least 30 seconds old for faster detection)
        if (oldestTime > 0 && (now - oldestTime) >= 30000) {
            float voltageChange = _cachedStatus.voltage - oldestVoltage;

            // If voltage increased by more than threshold, we're charging
            if (voltageChange > CHARGE_THRESHOLD) {
                if (!_cachedStatus.charging) {
                    _cachedStatus.charging = true;
                    Serial.printf("Battery: Charging detected via trend (%.3fV -> %.3fV, +%.3fV)\n",
                                 oldestVoltage, _cachedStatus.voltage, voltageChange);
                }
            } else if (_cachedStatus.voltage < HIGH_VOLTAGE_THRESHOLD && voltageChange < -CHARGE_THRESHOLD) {
                // Voltage is dropping and below high threshold = not charging
                if (_cachedStatus.charging) {
                    _cachedStatus.charging = false;
                    Serial.printf("Battery: Discharging detected (%.3fV -> %.3fV, %.3fV)\n",
                                 oldestVoltage, _cachedStatus.voltage, voltageChange);
                }
            }
        }
    }

    // Check for critical low battery
    if (isCriticallyLow()) {
        Serial.println("CRITICAL: Battery voltage too low! Shutting down...");
        shutdownLowBattery();
    }

    // Check for idle timeout (only if enabled and not charging)
    if (_sleepTimeoutMinutes > 0 && !_cachedStatus.charging) {
        unsigned long idleTime = now - _lastActivityTime;
        unsigned long timeoutMs = (unsigned long)_sleepTimeoutMinutes * 60 * 1000;
        if (idleTime >= timeoutMs) {
            Serial.printf("Idle timeout reached (%d minutes). Entering sleep...\n", _sleepTimeoutMinutes);
            enterIdleSleep();
        }
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

    // Preserve charging state from trend analysis
    bool currentCharging = _cachedStatus.charging;

    // Quick charging detection: if voltage increased since last read, we're likely charging
    if (_previousVoltage > 0 && voltage > _previousVoltage + 0.01f) {
        // Voltage increased by >10mV since last read - likely charging
        if (!currentCharging) {
            currentCharging = true;
            Serial.printf("Battery: Quick charge detect (%.3fV -> %.3fV, +%.3fV)\n",
                         _previousVoltage, voltage, voltage - _previousVoltage);
        }
    }

    // High voltage always means charging
    if (voltage >= HIGH_VOLTAGE_THRESHOLD) {
        currentCharging = true;
    }

    // Update previous voltage for next comparison
    _previousVoltage = voltage;

    // Update cache
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

void BatteryMgr::loadSleepSettings() {
    // Load from EbookFS partition
    if (EbookFS.exists("/sleep_config.json")) {
        File file = EbookFS.open("/sleep_config.json", "r");
        if (file) {
            DynamicJsonDocument doc(512);
            if (!deserializeJson(doc, file)) {
                _sleepTimeoutMinutes = doc["sleepTimeout"] | 5;
                _sleepMessage = doc["sleepMessage"] | "Press button to wake";
                Serial.printf("Loaded sleep settings: timeout=%d min, message=%s\n",
                             _sleepTimeoutMinutes, _sleepMessage.c_str());
            }
            file.close();
        }
    } else {
        // Use defaults
        _sleepTimeoutMinutes = 5;
        _sleepMessage = "Press button to wake";
        Serial.println("Using default sleep settings (no config file)");
    }
}

void BatteryMgr::resetIdleTimer() {
    _lastActivityTime = millis();
}

void BatteryMgr::enterIdleSleep() {
    Serial.println("Entering idle sleep...");
    Serial.printf("Sleep message: %s\n", _sleepMessage.c_str());
    Serial.flush();

    // Display sleep message on e-ink
    Book32Display& display = DisplayMgr::getInstance().getDisplay();
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setFont(&FreeSans18pt7b);
        display.setTextColor(GxEPD_BLACK);

        // Calculate text bounds for centering
        int16_t tbx, tby;
        uint16_t tbw, tbh;
        display.getTextBounds(_sleepMessage.c_str(), 0, 0, &tbx, &tby, &tbw, &tbh);

        // Center the text on screen
        int16_t x = (display.width() - tbw) / 2 - tbx;
        int16_t y = (display.height() - tbh) / 2 - tby;

        display.setCursor(x, y);
        display.print(_sleepMessage);
    } while (display.nextPage());

    // Wait for display to finish updating
    delay(100);

    // Configure wake sources
    // Wake on button press (GPIO5 on TRMNL, active LOW)
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BUTTON, 0);  // 0 = wake on LOW

    // Enter deep sleep
    Serial.println("Going to deep sleep...");
    Serial.flush();
    delay(50);
    esp_deep_sleep_start();
}
