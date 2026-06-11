#include "UIManager.h"
#include "AppLog.h"

#if defined(MINI_AZAN_UI_ENABLE) && MINI_AZAN_UI_ENABLE
#include "ui/UiPanel.h"
#endif

#ifndef MINI_AZAN_UI_ENABLE
#define MINI_AZAN_UI_ENABLE 0
#endif

bool UIManager::requestStopAzan(UiBridge& bridge) {
    UiCommand cmd{};
    cmd.cmd = UiCmd::StopAudio;
    return bridge.postUrgent(cmd, pdMS_TO_TICKS(10));
}

bool UIManager::begin(UiBridge& bridge) {
    Config cfg;
    return begin(bridge, cfg);
}

bool UIManager::begin(UiBridge& bridge, const Config& cfg) {
    _bridge = &bridge;
    _cfg = cfg;

#if !MINI_AZAN_UI_ENABLE
    return false;
#else
    if (!UiPanel::instance().begin(_cfg.width, _cfg.height)) {
        return false;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        taskEntry,
        "LVGL",
        _cfg.taskStackWords,
        this,
        _cfg.taskPriority,
        &_task,
        _cfg.taskCore);

    if (ok != pdPASS) {
        appLogf(APP_LOG_ERROR, "UI",
                "LVGL task create failed stack=%lu core=%d prio=%u",
                (unsigned long)_cfg.taskStackWords,
                (int)_cfg.taskCore,
                (unsigned)_cfg.taskPriority);
        return false;
    }

    _running = true;
    _nav.reset(UiScreenId::Home);
    appLogf(APP_LOG_INFO, "UI",
            "LVGL task ok stack=%lu core=%d prio=%u",
            (unsigned long)_cfg.taskStackWords,
            (int)_cfg.taskCore,
            (unsigned)_cfg.taskPriority);
    return true;
#endif
}

void UIManager::poll() {
    (void)0;
}

void UIManager::taskLoop() {
#if MINI_AZAN_UI_ENABLE
    appLog(APP_LOG_INFO, "UI", "LVGL task start");
    _screens.begin(*_bridge, _nav);
    appLog(APP_LOG_INFO, "UI", "LVGL home screen built");

    while (true) {
        _screens.tickRefresh();
        drainEvents();
        for (int i = 0; i < 4; i++) {
            lv_timer_handler();
        }
        vTaskDelay(pdMS_TO_TICKS(_cfg.timerPeriodMs));
    }
#endif
}

void UIManager::pumpLvgl() {
#if MINI_AZAN_UI_ENABLE
    lv_timer_handler();
#endif
}

void UIManager::drainEvents() {
#if MINI_AZAN_UI_ENABLE
    if (!_bridge) return;
    UiEventPayload ev{};
    while (_bridge->popEvent(ev, 0)) {
        _screens.onEvent(ev);
    }
#endif
}

void UIManager::buildScreen(UiScreenId id) {
#if MINI_AZAN_UI_ENABLE
    _screens.show(id);
#endif
    (void)id;
}

void UIManager::taskEntry(void* arg) {
    static_cast<UIManager*>(arg)->taskLoop();
}
