#pragma once

#include <Arduino.h>
#include "RtcDriver.h"

// RTC-first time with NTP fallback and automatic RTC discipline when online.
class TimeManager {
public:
    enum class Source : uint8_t {
        None = 0,
        Rtc,
        Ntp
    };

    struct Config {
        const char* ntpServer = "pool.ntp.org";
        long gmtOffsetSec = 2 * 3600;
        int daylightOffsetSec = 3600;
        int minValidYear = RtcDriver::MIN_VALID_YEAR;
        uint32_t rtcResyncIntervalMs = 60 * 1000UL;
        uint32_t ntpRtcDisciplineIntervalMs = 3600 * 1000UL;
        uint32_t ntpPollIntervalMs = 500;
        int ntpMaxAttempts = 40;
    };

    using LogFn = void (*)(int level, const char* tag, const char* message);

    bool begin(const Config& cfg, LogFn logFn = nullptr);
    void setWifiConnected(bool connected);

    // Call every loop() — non-blocking NTP state machine + periodic RTC→system refresh.
    void update();

    bool getCurrentTime(struct tm& out, uint32_t waitMs = 0) const;
    Source activeSource() const { return _source; }
    bool rtcUsable() const { return _rtcUsable; }
    bool rtcBatterySuspect() const { return _rtcBatterySuspect; }

    // Force an NTP attempt (non-blocking); only used when RTC unavailable/invalid.
    void requestNtpSync();

private:
    enum class NtpState : uint8_t { Idle, Waiting, Done, Failed };

    void logf(int level, const char* tag, const char* fmt, ...) const;
    bool applyRtcToSystem();
    bool applySystemFromNtp();
    void disciplineRtcFromSystem();
    void startNtpIfNeeded();
    void pollNtp();

    Config _cfg{};
    LogFn _log = nullptr;
    RtcDriver _rtc;

    Source _source = Source::None;
    bool _rtcUsable = false;
    bool _rtcBatterySuspect = false;
    bool _wifiConnected = false;

    NtpState _ntpState = NtpState::Idle;
    uint32_t _ntpStartedMs = 0;
    int _ntpAttempts = 0;
    bool _ntpConfigured = false;

    uint32_t _lastRtcResyncMs = 0;
    uint32_t _lastRtcDisciplineMs = 0;
};
