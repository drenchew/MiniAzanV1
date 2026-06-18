#pragma once

#include "ui/AppCoordinator.h"
#include "ui/UiBridge.h"
#include "TimeManager.h"
#include "PrayerScheduler.h"
#include "system/BluetoothManager.h"
#include "system/AppScheduler.h"

/** Core-1 SysCoord task — runs AppScheduler only (never LVGL). */
class SystemCoordinator {
public:
    struct Config {
        UBaseType_t taskPriority = 3;
        uint32_t taskStackWords = 10240;
        BaseType_t taskCore = 1;
        uint32_t periodMs = 10;
    };

    bool begin(UiBridge& bridge, AppCoordinator& coord, TimeManager& time,
               PrayerScheduler& prayer, BluetoothManager& bt, const Config& cfg);
    void setIsAudioPlayingPtr(bool* ptr);
    void poll();

private:
    static void taskEntry(void* arg);
    void taskLoop();

    AppScheduler _scheduler{};
    UiBridge* _bridge = nullptr;
    AppCoordinator* _coord = nullptr;
    TimeManager* _time = nullptr;
    PrayerScheduler* _prayer = nullptr;
    BluetoothManager* _bt = nullptr;
    bool* _isAudioPlaying = nullptr;
    Config _cfg{};
    TaskHandle_t _task = nullptr;
};
