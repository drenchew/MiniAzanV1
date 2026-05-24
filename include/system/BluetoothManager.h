#pragma once

#include "system/BluetoothTransferJob.h"

/**
 * Placeholder for future Bluetooth Classic SPP file transfer.
 * Isolated from LVGL, AudioManager, and StorageManager internals.
 *
 * Future flow:
 *   Phone/PC → BluetoothManager → AppCoordinator → StorageJobQueue → SD
 */
class BluetoothManager {
public:
    using ProgressFn = void (*)(const BluetoothTransferJob& job, void* user);

    bool begin();
    void poll();

    bool isEnabled() const { return _enabled; }
    bool isConnected() const { return _connected; }
    bool isTransferActive() const;
    const BluetoothTransferJob& activeJob() const { return _job; }

    /** UI / coordinator: enter transfer mode (no-op until BT stack added). */
    bool requestTransferMode(bool on);
    bool requestCancelTransfer();

    void setProgressCallback(ProgressFn fn, void* user);

private:
    void emitProgress();

    bool _enabled = false;
    bool _connected = false;
    BluetoothTransferJob _job{};
    ProgressFn _progressFn = nullptr;
    void* _progressUser = nullptr;
};
