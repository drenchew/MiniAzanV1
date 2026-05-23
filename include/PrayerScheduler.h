#pragma once

#include "AppTypes.h"
#include "TimeManager.h"
#include "StorageManager.h"

class PrayerScheduler {
public:
    struct Config {
        const char* prayerBinPath = "/prayer_times.bin";
        size_t recordSize = 12;
        uint32_t scheduleLogIntervalMs = 60 * 1000UL;
    };

    struct Hooks {
        void (*playAzan)() = nullptr;
        bool (*isAudioPlaying)() = nullptr;
        void (*setAudioPlaying)(bool) = nullptr;
        void (*setCurrentFile)(const char*) = nullptr;
        const char* (*getAzanPath)() = nullptr;
        bool* preFajrEnabled = nullptr;
        int* lastPreFajrDay = nullptr;
    };

    bool begin(TimeManager& time, StorageManager& storage, const Config& cfg, const Hooks& hooks);
    void update();
    void invalidateCache();

    int minutesToNextPrayer() const;
    bool getTodayTimes(DayRecord& out) const;

    void logSchedule() const;

private:
    bool ensureTodayCache(int yday);
    void checkTriggers(const PrayerNow& now);

    TimeManager* _time = nullptr;
    StorageManager* _storage = nullptr;
    Config _cfg{};
    Hooks _hooks{};

    DayRecord _cached{};
    int _cachedYday = -1;
    bool _cacheValid = false;
    int _lastCheckedMinute = -1;
    uint32_t _lastScheduleLogMs = 0;
};
