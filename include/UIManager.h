#pragma once

#include "SpiArchitecture.h"
#include "ui/UiBridge.h"
#include "ui/UiScreenMachine.h"

#if defined(MINI_AZAN_UI_ENABLE) && MINI_AZAN_UI_ENABLE
#include "ui/UiScreens.h"
#endif

/**
 * LVGL + touch on HSPI only (MINI_AZAN_UI_ENABLE=1).
 * Never includes SD.h or Audio.h — posts UiCommand via UiBridge.
 */
class UIManager {
public:
    struct Config {
        int width = 240;
        int height = 320;
        UBaseType_t taskPriority = 1;
        uint32_t taskStackWords = 8192;
        BaseType_t taskCore = 1;
        uint32_t timerPeriodMs = 5;
    };

    bool begin(UiBridge& bridge);
    bool begin(UiBridge& bridge, const Config& cfg);
    void poll();

    bool isActive() const { return _running; }

    /** From any task: instant STOP (urgent queue). */
    static bool requestStopAzan(UiBridge& bridge);

private:
    static void taskEntry(void* arg);
    void taskLoop();
    void pumpLvgl();
    void drainEvents();
    void buildScreen(UiScreenId id);

    UiBridge* _bridge = nullptr;
    Config _cfg{};
    UiScreenMachine _nav{};
#if defined(MINI_AZAN_UI_ENABLE) && MINI_AZAN_UI_ENABLE
    UiScreens _screens{};
#endif
    TaskHandle_t _task = nullptr;
    volatile bool _running = false;
};
