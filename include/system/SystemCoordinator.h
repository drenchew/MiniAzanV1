#pragma once

#include "ui/AppCoordinator.h"
#include "ui/UiBridge.h"
#include "TimeManager.h"
#include "PrayerScheduler.h"
#include "system/NetworkManager.h"

/**
 * Core-1 application task: routes UI commands, prayer scheduler, network poll.
 * Never calls LVGL — UI task owns display/touch.
 */
class SystemCoordinator {
public:
    struct Config {
        UBaseType_t taskPriority = 3;
        uint32_t taskStackWords = 8192;
        BaseType_t taskCore = 1;
        uint32_t periodMs = 10;
    };

    bool begin(UiBridge& bridge, AppCoordinator& coord, TimeManager& time,
               PrayerScheduler& prayer, NetworkManager& net, const Config& cfg);

    void poll();

private:
    static void taskEntry(void* arg);
    void taskLoop();

    UiBridge* _bridge = nullptr;
    AppCoordinator* _coord = nullptr;
    TimeManager* _time = nullptr;
    PrayerScheduler* _prayer = nullptr;
    NetworkManager* _net = nullptr;
    Config _cfg{};
    TaskHandle_t _task = nullptr;
};
