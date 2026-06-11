#pragma once

#include "system/BluetoothTransferJob.h"
#include <esp_a2dp_api.h>

class BluetoothA2DPSink;

/**
 * Bluetooth Classic owner: A2DP audio sink plus future SPP file transfer.
 * Isolated from LVGL, AudioManager, and StorageManager internals.
 */
class BluetoothManager {
public:
    using ProgressFn = void (*)(const BluetoothTransferJob& job, void* user);

    struct Pins {
        int bclk = 26;
        int lrc = 25;
        int dout = 27;
    };

    struct Config {
        Pins pins{};
        const char* deviceName = "MiniAzan Speaker";
        uint8_t defaultStreamVolume = 80; // 0-100 UI scale
    };

    bool begin(const Config& cfg);
    void poll();

    bool isEnabled() const { return _enabled; }
    bool isConnected() const { return _connected; }
    bool isTransferActive() const;
    const BluetoothTransferJob& activeJob() const { return _job; }

    bool isStreamingEnabled() const { return _streamingEnabled; }
    bool isStreamingActive() const { return _streamingActive; }
    uint8_t streamVolumePct() const { return _streamVolumePct; }

    /** UI / coordinator: enter transfer mode (no-op until BT stack added). */
    bool requestTransferMode(bool on);
    bool requestCancelTransfer();
    bool requestStreamingMode(bool on);
    bool requestSetStreamVolume(uint8_t pct);

    void setProgressCallback(ProgressFn fn, void* user);

private:
    static void connectionStateThunk(esp_a2d_connection_state_t state, void* user);
    static void audioStateThunk(esp_a2d_audio_state_t state, void* user);
    void onConnectionState(esp_a2d_connection_state_t state);
    void onAudioState(esp_a2d_audio_state_t state);
    void emitProgress();
    uint8_t toA2dpVolume(uint8_t pct) const;

    Config _cfg{};
    bool _enabled = false;
    bool _connected = false;
    bool _streamingEnabled = false;
    bool _streamingActive = false;
    bool _a2dpStarted = false;
    uint8_t _streamVolumePct = 80;
    BluetoothTransferJob _job{};
    BluetoothA2DPSink* _a2dpSink = nullptr;
    ProgressFn _progressFn = nullptr;
    void* _progressUser = nullptr;
};
