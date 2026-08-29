#include "TimeMgr.h"
#include <WiFi.h>
#include <time.h>

namespace {

// UTC, with no DST rules: the browser owns the timezone (see TimeMgr.h).
const long GMT_OFFSET_SEC = 0;
const int DAYLIGHT_OFFSET_SEC = 0;

} // namespace

TimeMgr& TimeMgr::getInstance() {
    static TimeMgr instance;
    return instance;
}

void TimeMgr::syncIfNeeded() {
    if (_sntpStarted) return;
    // Station mode only. In pure AP mode (the Book32 hotspot) there is no route
    // to the pool, and starting SNTP there would just retry forever.
    if (WiFi.status() != WL_CONNECTED) return;

    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");
    _sntpStarted = true;
    Serial.println("TimeMgr: SNTP started");
}

bool TimeMgr::nowEpoch(uint32_t& out) {
    time_t now = time(nullptr);
    if (now < (time_t)TIME_PLAUSIBLE_EPOCH) return false;
    out = (uint32_t)now;
    return true;
}

uint32_t TimeMgr::nowOrZero() {
    uint32_t now = 0;
    nowEpoch(now);
    return now;
}

bool TimeMgr::isSynced() {
    uint32_t ignored = 0;
    return nowEpoch(ignored);
}
