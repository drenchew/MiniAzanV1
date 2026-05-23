#pragma once

#include <Arduino.h>
#include "RtcDriver.h"

/** Wall-clock snapshot for prayer logic — always from RTC (+ optional debug offset). */
struct PrayerNow {
    bool valid = false;
    int year = 0;
    int month = 0;
    int day = 0;
    int yday = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int totalMinutes = 0;
    bool debugOffsetActive = false;
    int debugOffsetMinutes = 0;
};

// RTC-first time; ESP32 system clock is diagnostic/secondary only.
class TimeManager {
public:
    enum class Source : uint8_t {
        None = 0,
        Rtc,
        NtpFallback
    };

    struct Config {
        const char* ntpServer = "pool.ntp.org";
        long gmtOffsetSec = 2 * 3600;
        int daylightOffsetSec = 3600;
        int minValidYear = RtcDriver::MIN_VALID_YEAR;
        uint32_t rtcPollIntervalMs = 500;
        uint32_t diagIntervalMs = 60 * 1000UL;
        int driftWarnSec = 30;
        uint32_t ntpPollIntervalMs = 500;
        int ntpMaxAttempts = 40;
    };

    using LogFn = void (*)(int level, const char* tag, const char* message);

    bool begin(const Config& cfg, LogFn logFn = nullptr);
    void setWifiConnected(bool connected);

    /** Call every loop — refresh RTC cache, NTP→RTC only when RTC invalid. */
    void update();

    /** Authoritative for prayer / azan / scheduling (RTC + debug offset). */
    bool getPrayerNow(PrayerNow& out);

    /** Raw RTC chip time (no debug offset). */
    bool readRtcTime(struct tm& out);

    /** ESP32 software clock — diagnostics only. */
    bool readSystemTime(struct tm& out) const;

    int getSystemDriftSec() const { return _lastDriftSec; }
    uint32_t getLastRtcReadAgeMs() const;

    void setDebugOffsetMinutes(int minutes);
    int getDebugOffsetMinutes() const { return _debugOffsetMinutes; }

    Source activeSource() const { return _source; }
    bool rtcUsable() const { return _rtcUsable; }
    bool rtcBatterySuspect() const { return _rtcBatterySuspect; }

    void requestNtpSync();

    /** @deprecated Use getPrayerNow — kept for UI bridge migration. */
    bool getCurrentTime(struct tm& out, uint32_t waitMs = 0) const;

    static void formatTm(const struct tm& t, char* buf, size_t len);
    static void formatPrayerMinutes(int minutes, char* buf, size_t len);

private:
    enum class NtpState : uint8_t { Idle, Waiting, Done, Failed };

    void logf(int level, const char* tag, const char* fmt, ...) const;
    bool refreshRtcCache();
    void advanceCachedRtcByElapsed();
    bool buildPrayerNow(PrayerNow& out) const;
    bool applyOffset(PrayerNow& p) const;
    bool syncRtcFromNtp();
    void startNtpIfNeeded();
    void pollNtp();
    void runDiagnostics();

    Config _cfg{};
    LogFn _log = nullptr;
    RtcDriver _rtc;

    Source _source = Source::None;
    bool _rtcUsable = false;
    bool _rtcBatterySuspect = false;
    bool _wifiConnected = false;
    int _debugOffsetMinutes = 0;

    struct tm _cachedRtc{};
    bool _cachedRtcValid = false;
    uint32_t _cachedRtcMillis = 0;
    uint32_t _lastRtcPollMs = 0;
    uint32_t _lastDiagMs = 0;
    int _lastDriftSec = 0;
    uint32_t _updateCount = 0;

    NtpState _ntpState = NtpState::Idle;
    uint32_t _ntpStartedMs = 0;
    int _ntpAttempts = 0;
    bool _ntpConfigured = false;
};
