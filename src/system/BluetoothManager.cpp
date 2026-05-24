#include "system/BluetoothManager.h"
#include "system/AzanSafeMode.h"
#include "AppLog.h"

bool BluetoothManager::begin() {
    _enabled = false;
    _connected = false;
    _job = {};
    _job.phase = BtTransferPhase::Idle;
    appLog(APP_LOG_INFO, "BT", "BluetoothManager ready (SPP transfer not implemented)");
    return true;
}

void BluetoothManager::poll() {
    if (_job.phase == BtTransferPhase::Receiving && AzanSafeMode::isActive()) {
        _job.phase = BtTransferPhase::PausedForAzan;
        appLog(APP_LOG_WARN, "BT", "transfer paused — azan playing");
        emitProgress();
    }
}

bool BluetoothManager::isTransferActive() const {
    return _job.phase == BtTransferPhase::Receiving
        || _job.phase == BtTransferPhase::Writing
        || _job.phase == BtTransferPhase::QueuedForSd;
}

bool BluetoothManager::requestTransferMode(bool on) {
    if (on && !AzanSafeMode::allowBluetoothTransfer()) {
        appLog(APP_LOG_WARN, "BT", "transfer mode blocked during azan");
        return false;
    }
    _enabled = on;
    if (!on) {
        _connected = false;
        _job = {};
        _job.phase = BtTransferPhase::Idle;
    } else {
        _job.phase = BtTransferPhase::AwaitingConnection;
        appLog(APP_LOG_INFO, "BT", "transfer mode ON (awaiting future SPP stack)");
    }
    emitProgress();
    return true;
}

bool BluetoothManager::requestCancelTransfer() {
    _job = {};
    _job.phase = BtTransferPhase::Idle;
    _connected = false;
    emitProgress();
    return true;
}

void BluetoothManager::setProgressCallback(ProgressFn fn, void* user) {
    _progressFn = fn;
    _progressUser = user;
}

void BluetoothManager::emitProgress() {
    if (_progressFn) {
        _progressFn(_job, _progressUser);
    }
}
