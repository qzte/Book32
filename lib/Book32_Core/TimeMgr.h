#pragma once
// Book32 — wall-clock time, for the reading dates.
//
// The device has no battery-backed RTC (see the note in ProgressStore.h, which
// is why reading progress is ordered by the monotonic `seq` rather than by a
// timestamp). It does have the ESP32 internal clock, and ESP-IDF keeps system
// time running across deep sleep, so a single NTP sync per power cycle is
// enough to date everything until the next power cut.
//
// The important consequence for callers: **the clock can be unknown**, and that
// is a normal state, not an error. After a power cut there is no time at all
// until the next WiFi connection. In that window nowEpoch() returns false and
// the caller must store 0 rather than guess — a wrong date in a reading history
// is worse than an absent one, because nothing later can tell it was wrong.
//
// Everything is UTC. The web UI renders in the browser's local time, which is
// the only place that actually knows the user's timezone.

#include <Arduino.h>

// Anything before 2020-01-01 UTC is the ESP32's power-on epoch rather than a
// real time. Shared with the web layer, which applies the same floor to the
// timestamp a browser sends: one definition, so the two cannot drift apart.
static const uint32_t TIME_PLAUSIBLE_EPOCH = 1577836800UL;

class TimeMgr {
  public:
    static TimeMgr& getInstance();

    // Points SNTP at the pool, once, if the station interface is connected.
    // Idempotent and cheap: safe to call from every place that notices the
    // network came up, which is what makes it reliable without a WiFi event
    // handler.
    void syncIfNeeded();

    // Fills `out` with epoch seconds UTC and returns true only when the clock
    // is plausible. Returns false — leaving `out` untouched — while it is not.
    bool nowEpoch(uint32_t& out);

    // The same, for the many callers that store 0 for unknown anyway.
    uint32_t nowOrZero();

    // Whether the clock has been set this power cycle. For the web UI, so it
    // can explain why dates are missing instead of showing bare dashes.
    bool isSynced();

  private:
    TimeMgr() {}
    bool _sntpStarted = false;
};
