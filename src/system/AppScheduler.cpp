#include "system/AppScheduler.h"
#include "system/AzanSafeMode.h"
#include "system/SchedPriority.h"
#include "AppLog.h"

bool AppScheduler::begin(const Services& svc) {
    Config cfg{};
    return begin(svc, cfg);
}

bool AppScheduler::begin(const Services& svc, const Config& cfg) {
    _svc = svc;
    _cfg = cfg;
    if (!_svc.bridge || !_svc.coord) {
        return false;
    }
    appLog(APP_LOG_INFO, "SCHED", "AppScheduler ready (P0 fast-lane enabled)");
    return true;
}

uint8_t AppScheduler::commandPriority(UiCmd cmd) {
    return AppCoordinator::commandPriority(cmd);
}

void AppScheduler::runP0FastLane() {
    if (!_svc.bridge || !_svc.coord) return;

    UiCommand cmd{};
    while (_svc.bridge->popUrgent(cmd, 0)) {
        if (cmd.cmd == UiCmd::StopAudio) {
            _svc.coord->executeEmergencyStop(_svc.isAudioPlaying);
            appLog(APP_LOG_INFO, "SCHED", "P0 STOP_AZAN fast-lane");
        } else {
            _svc.coord->dispatchCommand(cmd);
        }
    }
}

void AppScheduler::runP1RealTime() {
    if (!AzanSafeMode::allowPriority(SchedPriority::P1_RealTime)) {
        return;
    }
    if (!_svc.coord) return;

    UiCommand cmd{};
    while (_svc.coord->popDeferredAtPriority(SchedPriority::P1_RealTime, cmd)) {
        _svc.coord->dispatchCommand(cmd);
    }
}

void AppScheduler::runP2SystemCore() {
    if (!AzanSafeMode::allowPriority(SchedPriority::P2_SystemCore)) {
        return;
    }
    if (_svc.time) {
        _svc.time->update();
    }
    if (_svc.prayer) {
        _svc.prayer->update();
    }
    if (!_svc.coord) return;

    UiCommand cmd{};
    unsigned processed = 0;
    while (processed < 2 && _svc.coord->popDeferredAtPriority(SchedPriority::P2_SystemCore, cmd)) {
        _svc.coord->dispatchCommand(cmd);
        processed++;
    }
}

void AppScheduler::runP3UserActions() {
    if (!AzanSafeMode::allowPriority(SchedPriority::P3_UserAction)) {
        return;
    }
    if (!_svc.coord) return;

    UiCommand cmd{};
    unsigned processed = 0;
    while (processed < _cfg.maxP3PerTick
           && _svc.coord->popDeferredAtPriority(SchedPriority::P3_UserAction, cmd)) {
        _svc.coord->dispatchCommand(cmd);
        processed++;
    }
}

void AppScheduler::runP4HeavyIo() {
    if (_svc.bluetooth) {
        _svc.bluetooth->poll();
    }
    if (_svc.coord) {
        _svc.coord->pollStorageResults();
        for (uint8_t i = 0; i < _cfg.maxP4StorageTicks; i++) {
            if (!_svc.coord->tickStorageWorker()) {
                break;
            }
        }
        if (AzanSafeMode::allowPriority(SchedPriority::P4_HeavyIo)) {
            UiCommand cmd{};
            while (_svc.coord->popDeferredAtPriority(SchedPriority::P4_HeavyIo, cmd)) {
                _svc.coord->dispatchCommand(cmd);
            }
        }
    }
}

void AppScheduler::poll() {
    if (_svc.coord) {
        _svc.coord->ingestCommands();
    }
    runP0FastLane();
    runP1RealTime();
    runP2SystemCore();
    runP3UserActions();
    runP4HeavyIo();
}
