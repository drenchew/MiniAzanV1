#pragma once

#include "ui/UiBridge.h"
#include "ui/UiTypes.h"
#include "ui/AppCoordinator.h"
#include "TimeManager.h"
#include "PrayerScheduler.h"
#include "system/BluetoothManager.h"

/**
 * Central deterministic scheduler (AUTOSAR-like).
 *
 *   UI → UiBridge → AppCoordinator handlers ← AppScheduler::poll()
 *
 * P0 STOP_AZAN bypasses queues and executes immediately on the SysCoord task.
 */
class AppScheduler {
public:
    struct Config {
        uint32_t periodMs = 10;
        uint8_t maxP3PerTick = 4;
        uint8_t maxP4StorageTicks = 8;
    };

    struct Services {
        UiBridge* bridge = nullptr;
        AppCoordinator* coord = nullptr;
        TimeManager* time = nullptr;
        PrayerScheduler* prayer = nullptr;
        BluetoothManager* bluetooth = nullptr;
        bool* isAudioPlaying = nullptr;
    };

    bool begin(const Services& svc);
    bool begin(const Services& svc, const Config& cfg);
    void poll();

private:
    void runP0FastLane();
    void runP1RealTime();
    void runP2SystemCore();
    void runP3UserActions();
    void runP4HeavyIo();

    static uint8_t commandPriority(UiCmd cmd);

    Services _svc{};
    Config _cfg{};
};
