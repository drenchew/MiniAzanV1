#include "system/SystemCoordinator.h"
#include "system/AppScheduler.h"
#include "AppLog.h"

bool SystemCoordinator::begin(UiBridge& bridge, AppCoordinator& coord, TimeManager& time,
                              PrayerScheduler& prayer, BluetoothManager& bt,
                              const Config& cfg) {
    _bridge = &bridge;
    _coord = &coord;
    _time = &time;
    _prayer = &prayer;
    _bt = &bt;
    _cfg = cfg;

    AppScheduler::Services svc{};
    svc.bridge = &bridge;
    svc.coord = &coord;
    svc.time = &time;
    svc.prayer = &prayer;
    svc.bluetooth = &bt;
    svc.isAudioPlaying = _isAudioPlaying;

    AppScheduler::Config scfg{};
    scfg.periodMs = _cfg.periodMs;
    if (!_scheduler.begin(svc, scfg)) {
        appLog(APP_LOG_ERROR, "SYSCO", "AppScheduler init failed");
        return false;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        taskEntry, "SysCoord", _cfg.taskStackWords, this,
        _cfg.taskPriority, &_task, _cfg.taskCore);

    if (ok != pdPASS) {
        appLog(APP_LOG_ERROR, "SYSCO", "task create failed");
        return false;
    }
    appLogf(APP_LOG_INFO, "SYSCO", "task ok core=%d prio=%u", (int)_cfg.taskCore, (unsigned)_cfg.taskPriority);
    return true;
}

void SystemCoordinator::setIsAudioPlayingPtr(bool* ptr) {
    _isAudioPlaying = ptr;
}

void SystemCoordinator::poll() {
    _scheduler.poll();
}

void SystemCoordinator::taskEntry(void* arg) {
    static_cast<SystemCoordinator*>(arg)->taskLoop();
}

void SystemCoordinator::taskLoop() {
    for (;;) {
        poll();
        vTaskDelay(pdMS_TO_TICKS(_cfg.periodMs));
    }
}
