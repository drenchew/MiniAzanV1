#include "PrayerScheduler.h"
#include "AppLog.h"

static const char* kPrayerNames[] = {"FAJR", "DUHA", "DHUHR", "ASR", "MAGHRIB", "ISHA"};

bool PrayerScheduler::begin(TimeManager& time, StorageManager& storage, const Config& cfg, const Hooks& hooks) {
    _time = &time;
    _storage = &storage;
    _cfg = cfg;
    _hooks = hooks;
    return _time && _storage && _hooks.playAzan && _hooks.getAzanPath;
}

void PrayerScheduler::invalidateCache() {
    _cacheValid = false;
    _cachedYday = -1;
}

bool PrayerScheduler::ensureTodayCache(int yday) {
    if (_cacheValid && _cachedYday == yday) return true;
    if (!_storage->readRecordAt(_cfg.prayerBinPath, yday, &_cached, _cfg.recordSize)) {
        _cacheValid = false;
        return false;
    }
    _cachedYday = yday;
    _cacheValid = true;
    return true;
}

void PrayerScheduler::logSchedule() const {
    if (!_cacheValid || !_time) return;
    PrayerNow now{};
    if (!_time->getPrayerNow(now) || !now.valid) return;

    char tbuf[8];
    for (int i = 0; i < 6; i++) {
        TimeManager::formatPrayerMinutes(_cached.times[i], tbuf, sizeof(tbuf));
        appLogf(APP_LOG_INFO, "PRAY",
                "schedule slot=%s time=%s yday=%d",
                kPrayerNames[i], tbuf, now.yday);
    }
    int next = minutesToNextPrayer();
    if (next >= 0) {
        appLogf(APP_LOG_INFO, "PRAY",
                "next_in_min=%d clock=%02d:%02d:%02d virt_off=%d",
                next, now.hour, now.minute, now.second, now.debugOffsetMinutes);
    }
}

void PrayerScheduler::checkTriggers(const PrayerNow& now) {
    if (!ensureTodayCache(now.yday)) {
        appLog(APP_LOG_WARN, "PRAY", "prayer_bin_read_failed");
        return;
    }

    if (now.minute == _lastCheckedMinute) return;
    _lastCheckedMinute = now.minute;

    int fajr = _cached.times[0];
    int preFajrTarget = fajr - 30;

    if (_hooks.preFajrEnabled && *_hooks.preFajrEnabled &&
        now.totalMinutes == preFajrTarget &&
        _hooks.lastPreFajrDay && *_hooks.lastPreFajrDay != now.yday) {
        char tb[8];
        TimeManager::formatPrayerMinutes(fajr, tb, sizeof(tb));
        appLogf(APP_LOG_INFO, "PRAY", "prefajr_trigger fajr_at=%s", tb);
        _hooks.playAzan();
        if (_hooks.setAudioPlaying) _hooks.setAudioPlaying(true);
        if (_hooks.setCurrentFile) _hooks.setCurrentFile(_hooks.getAzanPath());
        if (_hooks.lastPreFajrDay) *_hooks.lastPreFajrDay = now.yday;
        return;
    }

    for (int i = 0; i < 6; i++) {
        if (i == 1) continue;
        if (now.totalMinutes == _cached.times[i]) {
            char tb[8];
            TimeManager::formatPrayerMinutes(_cached.times[i], tb, sizeof(tb));
            appLogf(APP_LOG_INFO, "PRAY", "azan_trigger prayer=%s time=%s", kPrayerNames[i], tb);
            _hooks.playAzan();
            if (_hooks.setAudioPlaying) _hooks.setAudioPlaying(true);
            if (_hooks.setCurrentFile) _hooks.setCurrentFile(_hooks.getAzanPath());
            break;
        }
    }
}

void PrayerScheduler::update() {
    if (!_time) return;
    PrayerNow now{};
    if (!_time->getPrayerNow(now) || !now.valid) return;

    if (millis() - _lastScheduleLogMs >= _cfg.scheduleLogIntervalMs) {
        _lastScheduleLogMs = millis();
        ensureTodayCache(now.yday);
        logSchedule();
    }

    static uint32_t lastCheckMs = 0;
    if (millis() - lastCheckMs < 400) return;
    lastCheckMs = millis();
    checkTriggers(now);
}

bool PrayerScheduler::getTodayTimes(DayRecord& out) const {
    PrayerNow now{};
    if (!_time || !_time->getPrayerNow(now) || !now.valid) return false;
    if (!_cacheValid || _cachedYday != now.yday) {
        if (!_storage) return false;
        return _storage->readRecordAt(_cfg.prayerBinPath, now.yday, &out, _cfg.recordSize);
    }
    out = _cached;
    return true;
}

int PrayerScheduler::minutesToNextPrayer() const {
    PrayerNow now{};
    if (!_time || !_time->getPrayerNow(now) || !now.valid) return -1;

    DayRecord today{};
    if (_cacheValid && _cachedYday == now.yday) {
        today = _cached;
    } else if (!_storage || !_storage->readRecordAt(_cfg.prayerBinPath, now.yday, &today, _cfg.recordSize)) {
        return -1;
    }

    int nowMin = now.totalMinutes;
    for (int i = 0; i < 6; i++) {
        if (i == 1) continue;
        if (today.times[i] > nowMin) {
            return today.times[i] - nowMin;
        }
    }
    DayRecord tomorrow{};
    if (_storage && _storage->readRecordAt(_cfg.prayerBinPath, now.yday + 1, &tomorrow, _cfg.recordSize)) {
        return (1440 - nowMin) + tomorrow.times[0];
    }
    return -1;
}
