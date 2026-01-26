#include "BatteryMgr.h"
#include "../../include/Config.h"
#include "Config.h"

BatteryMgr::BatteryMgr() {}

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
}

float BatteryMgr::getVoltage() {
#ifdef PIN_VBAT_SWITCH
    digitalWrite(PIN_VBAT_SWITCH, VBAT_SWITCH_LEVEL); // Turn on measurement
    delay(5); // Wait for stabilization
#endif

    // Read ADC
    uint32_t raw = 0;
    // Average a few samples
    for(int i=0; i<10; i++) {
        raw += analogRead(PIN_BAT_VOLT);
        delay(1);
    }
    raw /= 10;
    
#ifdef PIN_VBAT_SWITCH
    digitalWrite(PIN_VBAT_SWITCH, !VBAT_SWITCH_LEVEL); // Turn off to save power
#endif

    // Convert to Voltage
    // XIAO ESP32-S3 divider is usually 1M/1M? Or 100k/100k?
    // Seeed TRMNL project uses:
    // float voltage = (raw / 4095.0) * 3.3 * 2.0;
    
    float voltage = (raw / 4095.0) * 3.3 * 2.0 * 1.0; // Adjust calibration if needed
    
    return voltage;
}

int BatteryMgr::getPercentage() {
    float v = getVoltage();
    // LiPo Curve Approximation
    // 4.2V = 100%, 3.2V = 0%
    if (v >= 4.2) return 100;
    if (v <= 3.2) return 0;
    
    int p = (int)((v - 3.2) / (4.2 - 3.2) * 100);
    return p;
}

bool BatteryMgr::isCharging() {
    // Basic heuristic: if voltage is > 4.22V, it's likely charging or fully charged and plugged in.
    // The T-Energy-S3 charge IC usually charges up to 4.2V, but while plugged in 
    // and charging, the voltage seen on the divider can be slightly higher due to the charging voltage.
    return getVoltage() > 4.22;
}
