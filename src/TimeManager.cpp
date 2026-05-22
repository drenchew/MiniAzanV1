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

bool TimeManager::begin(const Config& cfg, LogFn logFn) {
    _cfg = cfg;
    _log = logFn;
    _source = Source::None;
    _rtcUsable = false;
    _rtcBatterySuspect = false;
    _ntpState = NtpState::Idle;
    _ntpAttempts = 0;
    _ntpConfigured = false;
    _lastRtcResyncMs = 0;
    _lastRtcDisciplineMs = 0;

    if (!_rtc.begin()) {
        logf(LOG_WARN, "RTC", "DS3231 not found — NTP fallback when WiFi available");
        requestNtpSync();
        return true;
    }

    struct tm rtcTm{};
    if (!_rtc.readTime(rtcTm)) {
        logf(LOG_WARN, "RTC", "RTC present but read failed — NTP fallback");
        requestNtpSync();
        return true;
    }

    _rtcBatterySuspect = _rtc.hasLostPower() || !_rtc.isTimeValid(rtcTm);
    if (_rtcBatterySuspect) {
        logf(LOG_WARN, "RTC", "RTC time invalid or lost power (battery suspect)");
        requestNtpSync();
        return true;
    }

    if (applyRtcToSystem()) {
        _rtcUsable = true;
        _source = Source::Rtc;
        logf(LOG_INFO, "RTC", "Primary clock: DS3231 (battery OK)");
        return true;
    }

    logf(LOG_WARN, "RTC", "Could not apply RTC to system — NTP fallback");
    requestNtpSync();
    return true;
}

void TimeManager::setWifiConnected(bool connected) {
    if (connected && !_wifiConnected && (_source != Source::Rtc || _rtcBatterySuspect)) {
        requestNtpSync();
    }
    _wifiConnected = connected;
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
    logf(LOG_DEBUG, "NTP", "configTime started (non-blocking poll)");
}

bool TimeManager::applyRtcToSystem() {
    struct tm rtcTm{};
    if (!_rtc.readTime(rtcTm) || !_rtc.isTimeValid(rtcTm)) return false;

    time_t unixTime = mktime(&rtcTm);
    if (unixTime < 0) return false;

    struct timeval tv = {.tv_sec = unixTime, .tv_usec = 0};
    settimeofday(&tv, nullptr);
    _lastRtcResyncMs = millis();
    return true;
}

bool TimeManager::applySystemFromNtp() {
    struct tm timeinfo{};
    if (!::getLocalTime(&timeinfo, 0)) return false;
    if (!_rtc.isTimeValid(timeinfo)) return false;

    struct timeval tv = {.tv_sec = 0, .tv_usec = 0};
    if (gettimeofday(&tv, nullptr) != 0) return false;

    _source = Source::Ntp;
    logf(LOG_INFO, "NTP", "System time from NTP: %04d-%02d-%02d %02d:%02d:%02d",
         timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
         timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    if (_rtc.isPresent()) {
        if (_rtc.writeTime(timeinfo)) {
            _rtcUsable = true;
            _rtcBatterySuspect = false;
            _source = Source::Rtc;
            _lastRtcDisciplineMs = millis();
            logf(LOG_INFO, "RTC", "RTC written from NTP — switching to RTC primary");
        }
    }
    return true;
}

void TimeManager::disciplineRtcFromSystem() {
    if (!_rtcUsable || !_wifiConnected || _rtcBatterySuspect) return;
    if (millis() - _lastRtcDisciplineMs < _cfg.ntpRtcDisciplineIntervalMs) return;

    struct tm sysTm{};
    if (!::getLocalTime(&sysTm, 0)) return;
    if (_rtc.writeTime(sysTm)) {
        _lastRtcDisciplineMs = millis();
        logf(LOG_DEBUG, "RTC", "RTC disciplined from system (hourly)");
    }
}

void TimeManager::pollNtp() {
    if (_ntpState != NtpState::Waiting) return;

    struct tm timeinfo{};
    if (::getLocalTime(&timeinfo, 0) && _rtc.isTimeValid(timeinfo)) {
        applySystemFromNtp();
        _ntpState = NtpState::Done;
        return;
    }

    _ntpAttempts++;
    if (_ntpAttempts >= _cfg.ntpMaxAttempts) {
        _ntpState = NtpState::Failed;
        logf(LOG_WARN, "NTP", "NTP sync timed out after %d polls", _ntpAttempts);
        return;
    }

    if (millis() - _ntpStartedMs > (_cfg.ntpPollIntervalMs * _cfg.ntpMaxAttempts)) {
        _ntpState = NtpState::Failed;
        logf(LOG_WARN, "NTP", "NTP sync window expired");
    }
}

void TimeManager::update() {
    if (_rtcUsable && _source == Source::Rtc) {
        if (millis() - _lastRtcResyncMs >= _cfg.rtcResyncIntervalMs) {
            applyRtcToSystem();
        }
        if (_wifiConnected) {
            disciplineRtcFromSystem();
        }
    }

    pollNtp();

    if (_ntpState == NtpState::Failed && _rtcUsable) {
        applyRtcToSystem();
        _source = Source::Rtc;
        _ntpState = NtpState::Idle;
    }
}

bool TimeManager::getCurrentTime(struct tm& out, uint32_t waitMs) const {
    const uint32_t step = 10;
    uint32_t waited = 0;
    while (true) {
        if (::getLocalTime(&out, 0)) {
            return _rtc.isTimeValid(out);
        }
        if (waitMs == 0 || waited >= waitMs) return false;
        delay(step);
        waited += step;
    }
}
