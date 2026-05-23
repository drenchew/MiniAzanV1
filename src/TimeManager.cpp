#include "TimeManager.h"
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>

#ifndef LOG_ERROR
#define LOG_ERROR 0
#define LOG_WARN  1
#define LOG_INFO  2
#define LOG_DEBUG 3
#endif

void TimeManager::logf(int level, const char* tag, const char* fmt, ...) const {
    if (!_log) return;
    char buf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    _log(level, tag, buf);
}

void TimeManager::formatTm(const struct tm& t, char* buf, size_t len) {
    if (!buf || len < 9) return;
    snprintf(buf, len, "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
}

void TimeManager::formatPrayerMinutes(int minutes, char* buf, size_t len) {
    if (!buf || len < 6) return;
    if (minutes < 0) minutes += 1440;
    minutes %= 1440;
    snprintf(buf, len, "%02d:%02d", minutes / 60, minutes % 60);
}

bool TimeManager::begin(const Config& cfg, LogFn logFn) {
    _cfg = cfg;
    _log = logFn;
    _source = Source::None;
    _rtcUsable = false;
    _rtcBatterySuspect = false;
    _cachedRtcValid = false;
    _ntpState = NtpState::Idle;
    _ntpAttempts = 0;
    _ntpConfigured = false;
    _lastRtcPollMs = 0;
    _lastDiagMs = 0;
    _lastDriftSec = 0;

    if (!_rtc.begin()) {
        logf(LOG_WARN, "CLK", "RTC missing — NTP fallback when WiFi up");
        requestNtpSync();
        return true;
    }

    struct tm rtcTm{};
    if (!refreshRtcCache()) {
        logf(LOG_WARN, "CLK", "RTC read failed at boot");
        requestNtpSync();
        return true;
    }

    _rtcBatterySuspect = _rtc.hasLostPower() || !_rtc.isTimeValid(_cachedRtc);
    if (_rtcBatterySuspect) {
        logf(LOG_WARN, "CLK", "RTC invalid or lost power");
        requestNtpSync();
        return true;
    }

    _rtcUsable = true;
    _source = Source::Rtc;
    logf(LOG_INFO, "CLK", "Authoritative clock: DS3231 RTC");
    return true;
}

void TimeManager::setWifiConnected(bool connected) {
    if (connected && !_wifiConnected && (!_rtcUsable || _rtcBatterySuspect)) {
        requestNtpSync();
    }
    _wifiConnected = connected;
}

void TimeManager::setDebugOffsetMinutes(int minutes) {
    _debugOffsetMinutes = minutes;
}

void TimeManager::requestNtpSync() {
    if (!_wifiConnected) {
        _ntpState = NtpState::Idle;
        return;
    }
    _ntpState = NtpState::Waiting;
    _ntpStartedMs = millis();
    _ntpAttempts = 0;
    startNtpIfNeeded();
}

void TimeManager::startNtpIfNeeded() {
    if (_ntpConfigured) return;
    configTime(_cfg.gmtOffsetSec, _cfg.daylightOffsetSec, _cfg.ntpServer);
    _ntpConfigured = true;
}

bool TimeManager::readRtcTime(struct tm& out) {
    return _rtc.readTime(out);
}

bool TimeManager::readSystemTime(struct tm& out) const {
    return ::getLocalTime(&out, 0);
}

uint32_t TimeManager::getLastRtcReadAgeMs() const {
    if (!_cachedRtcValid) return UINT32_MAX;
    return millis() - _cachedRtcMillis;
}

bool TimeManager::refreshRtcCache() {
    struct tm t{};
    if (!_rtc.readTime(t) || !_rtc.isTimeValid(t)) {
        _cachedRtcValid = false;
        return false;
    }
    _cachedRtc = t;
    _cachedRtcValid = true;
    _cachedRtcMillis = millis();
    return true;
}

void TimeManager::advanceCachedRtcByElapsed() {
    if (!_cachedRtcValid) return;
    uint32_t elapsed = (millis() - _cachedRtcMillis) / 1000;
    if (elapsed == 0) return;

    int sec = _cachedRtc.tm_sec + (int)elapsed;
    int min = _cachedRtc.tm_min;
    int hour = _cachedRtc.tm_hour;
    int mday = _cachedRtc.tm_mday;

    if (sec >= 60) {
        min += sec / 60;
        sec %= 60;
    }
    if (min >= 60) {
        hour += min / 60;
        min %= 60;
    }
    if (hour >= 24) {
        mday += hour / 24;
        hour %= 24;
    }
    _cachedRtc.tm_sec = sec;
    _cachedRtc.tm_min = min;
    _cachedRtc.tm_hour = hour;
    _cachedRtc.tm_mday = mday;
    _cachedRtc.tm_isdst = -1;
    mktime(&_cachedRtc);
    _cachedRtcMillis += elapsed * 1000UL;
}

bool TimeManager::applyOffset(PrayerNow& p) const {
    if (_debugOffsetMinutes == 0) {
        p.debugOffsetActive = false;
        p.debugOffsetMinutes = 0;
        return true;
    }
    p.debugOffsetActive = true;
    p.debugOffsetMinutes = _debugOffsetMinutes;

    int totalSec = p.hour * 3600 + p.minute * 60 + p.second + (_debugOffsetMinutes * 60);
    int yday = p.yday;
    while (totalSec < 0) {
        totalSec += 86400;
        yday--;
        if (yday < 1) yday = 365;
    }
    while (totalSec >= 86400) {
        totalSec -= 86400;
        yday++;
        if (yday > 366) yday = 1;
    }
    p.yday = yday;
    p.hour = totalSec / 3600;
    p.minute = (totalSec % 3600) / 60;
    p.second = totalSec % 60;
    p.totalMinutes = p.hour * 60 + p.minute;
    return true;
}

bool TimeManager::buildPrayerNow(PrayerNow& out) const {
    if (!_cachedRtcValid) return false;
    struct tm t = _cachedRtc;
    out.valid = true;
    out.year = t.tm_year + 1900;
    out.month = t.tm_mon + 1;
    out.day = t.tm_mday;
    out.yday = t.tm_yday + 1;
    out.hour = t.tm_hour;
    out.minute = t.tm_min;
    out.second = t.tm_sec;
    out.totalMinutes = t.tm_hour * 60 + t.tm_min;
    return applyOffset(out);
}

bool TimeManager::getPrayerNow(PrayerNow& out) {
    if (!_cachedRtcValid && !refreshRtcCache()) {
        out.valid = false;
        return false;
    }
    return buildPrayerNow(out);
}

bool TimeManager::getCurrentTime(struct tm& out, uint32_t waitMs) const {
    (void)waitMs;
    if (!_cachedRtcValid) return false;
    out = _cachedRtc;
    return _rtc.isTimeValid(out);
}

bool TimeManager::syncRtcFromNtp() {
    struct tm ntpTm{};
    if (!::getLocalTime(&ntpTm, 0) || !_rtc.isTimeValid(ntpTm)) return false;
    if (!_rtc.writeTime(ntpTm)) return false;

    _rtcBatterySuspect = false;
    _rtcUsable = true;
    _source = Source::Rtc;
    refreshRtcCache();
    logf(LOG_INFO, "CLK", "RTC set from NTP wall time");
    return true;
}

void TimeManager::pollNtp() {
    if (_ntpState != NtpState::Waiting) return;

    if (syncRtcFromNtp()) {
        _ntpState = NtpState::Done;
        return;
    }

    _ntpAttempts++;
    if (_ntpAttempts >= _cfg.ntpMaxAttempts ||
        millis() - _ntpStartedMs > (_cfg.ntpPollIntervalMs * _cfg.ntpMaxAttempts)) {
        _ntpState = NtpState::Failed;
        logf(LOG_WARN, "CLK", "NTP→RTC sync failed");
    }
}

void TimeManager::runDiagnostics() {
    struct tm sysTm{};
    bool sysOk = readSystemTime(sysTm);
    int drift = 0;
    if (_cachedRtcValid && sysOk) {
        time_t rtcUnix = mktime(&_cachedRtc);
        time_t sysUnix = mktime(&sysTm);
        drift = (int)(sysUnix - rtcUnix);
        _lastDriftSec = drift;
    }

    char rtcBuf[16];
    char sysBuf[16];
    formatTm(_cachedRtc, rtcBuf, sizeof(rtcBuf));
    formatTm(sysTm, sysBuf, sizeof(sysBuf));

    logf(LOG_INFO, "CLK",
         "diag rtc=%s sys=%s drift_sec=%d rtc_age_ms=%lu src=%s batt=%s off=%d upd=%lu",
         _cachedRtcValid ? rtcBuf : "FAIL",
         sysOk ? sysBuf : "FAIL",
         drift,
         (unsigned long)getLastRtcReadAgeMs(),
         _source == Source::Rtc ? "RTC" : "NTP",
         _rtcBatterySuspect ? "BAD" : "OK",
         _debugOffsetMinutes,
         (unsigned long)_updateCount);

    if (_cachedRtcValid && sysOk && abs(drift) >= _cfg.driftWarnSec) {
        logf(LOG_WARN, "CLK", "RTC vs system drift %d sec — prayer uses RTC only", drift);
    }
    if (getLastRtcReadAgeMs() > 5000) {
        logf(LOG_WARN, "CLK", "RTC cache stale age_ms=%lu — update loop starved?",
             (unsigned long)getLastRtcReadAgeMs());
    }
}

void TimeManager::update() {
    _updateCount++;

    if (millis() - _lastRtcPollMs >= _cfg.rtcPollIntervalMs) {
        _lastRtcPollMs = millis();
        if (refreshRtcCache()) {
            _rtcUsable = true;
            _source = Source::Rtc;
            _rtcBatterySuspect = _rtc.hasLostPower() || !_rtc.isTimeValid(_cachedRtc);
        }
    } else if (_cachedRtcValid) {
        advanceCachedRtcByElapsed();
    }

    if (!_rtcUsable || _rtcBatterySuspect) {
        pollNtp();
    }

    if (millis() - _lastDiagMs >= _cfg.diagIntervalMs) {
        _lastDiagMs = millis();
        runDiagnostics();
    }
}
