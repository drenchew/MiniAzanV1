/**
 * @file  BluetoothAudioMode.h
 * @brief A2DP Bluetooth audio streaming to I2S speaker (independent from file playback).
 *
 * DESIGN
 * ──────
 * Runs on separate FreeRTOS task (Core 0, lower priority than azan playback).
 * Uses I2S DMA for audio output (shares pins with AudioManager but runs independently).
 * Automatically pauses during azan playback (respects AzanSafeMode).
 * Pre-allocates audio buffers from heap at boot to prevent fragmentation.
 *
 * PRIORITY HIERARCHY
 * ──────────────────
 *  1. Azan playback (AudioTask, priority 5) — interrupts BT streaming
 *  2. BT streaming (BluetoothAudioTask, priority 3) — pauses for azan
 *  3. System/UI (priority 1-2) — lowest priority
 *
 * THREAD-SAFETY
 * ─────────────
 * Commands queued from any task (UI, coordinator, etc.)
 * All A2DP callbacks run on internal Bluetooth task — safe queuing only.
 * I2S DMA shared between AudioManager and BluetoothAudioMode (mutual exclusion
 * not needed because only one plays at a time; azan has priority).
 */

#pragma once

#include <stdint.h>
#include <cstddef>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

class BluetoothAudioMode {
public:
    struct Config {
        UBaseType_t taskPriority = 3;        // Lower than azan task (5)
        uint32_t taskStackWords = 12288;     // For A2DP + I2S buffers
        BaseType_t taskCore = 0;             // Core 0 (audio core)
        uint32_t audioBufferSizeBytes = 4096;
        uint8_t numAudioBuffers = 4;
        uint32_t loopDelayMs = 1;
    };

    using LogFn = void (*)(int level, const char* tag, const char* message);

    /**
     * Initialize Bluetooth A2DP audio streaming mode.
     * Must be called after AudioManager::begin() but before BT connection expected.
     * Does NOT enable A2DP automatically — use requestStreamingMode(true).
     *
     * @param cfg Configuration (task priority, stack size, etc.)
     * @param logFn Optional logging callback
     * @return true if initialization successful
     */
    bool begin(const Config& cfg, LogFn logFn = nullptr);

    /**
     * Enable/disable Bluetooth audio streaming mode.
     * When enabled: accepts A2DP connections and streams audio to I2S.
     * When disabled: A2DP inactive, all audio routed to AudioManager (file playback).
     *
     * @param enable true to enable A2DP streaming, false to disable
     * @return true if successful
     */
    bool requestStreamingMode(bool enable);

    /** Check if Bluetooth audio streaming is currently active. */
    bool isStreamingActive() const { return _streamingActive; }
    
    /** Check if Bluetooth streaming mode is enabled (may not be actively streaming). */
    bool isStreamingEnabled() const { return _streamingEnabled; }

    /** Check if Bluetooth device is connected (but not necessarily streaming). */
    bool isDeviceConnected() const { return _deviceConnected; }

    /** Set playback volume (0–100). Independent from azan volume. */
    void setVolume(uint8_t volume);
    uint8_t getVolume() const { return _volume; }

    /** Poll for state updates (safe to call from any task). */
    void poll();

    /** Stop all streaming and disconnect (called before azan playback). */
    void pauseForAzan();

    /** Resume streaming if it was active before azan (called after azan ends). */
    void resumeAfterAzan();

    /**
     * Callback for A2DP connection state changes.
     * Called from internal Bluetooth task — must not block.
     */
    using ConnectionCallback = void (*)(bool connected, void* user);
    void setConnectionCallback(ConnectionCallback cb, void* user);

    /**
     * Callback for A2DP playback state changes (play/pause/stop).
     * Called from internal Bluetooth task — must not block.
     */
    using PlaybackCallback = void (*)(bool playing, void* user);
    void setPlaybackCallback(PlaybackCallback cb, void* user);

private:
    enum class CmdType : uint8_t {
        Stop = 0,
        SetVolume,
        PauseForAzan,
        ResumeAfterAzan,
        EnableStreaming,
        DisableStreaming
    };

    struct Command {
        CmdType type = CmdType::Stop;
        uint8_t volume = 0;
    };

    static void taskEntry(void* arg);
    void taskLoop();
    void drainCommands();
    bool initA2dp();
    void setupI2sForStreaming();
    void cleanupA2dp();

    void logf(int level, const char* tag, const char* fmt, ...) const;

    // Configuration
    Config _cfg{};
    LogFn _log = nullptr;

    // State
    volatile bool _streamingActive = false;
    volatile bool _deviceConnected = false;
    volatile bool _pausedForAzan = false;
    bool _streamingEnabled = false;
    uint8_t _volume = 50;

    // Task management
    QueueHandle_t _cmdQ = nullptr;
    TaskHandle_t _task = nullptr;

    // Audio buffers (pre-allocated at boot)
    uint8_t* _audioBuffers[4] = {nullptr, nullptr, nullptr, nullptr};
    size_t _audioBufferSize = 0;

    // Callbacks
    ConnectionCallback _connCb = nullptr;
    void* _connUser = nullptr;
    PlaybackCallback _playbackCb = nullptr;
    void* _playbackUser = nullptr;

    // A2DP instance (opaque, platform-specific)
    void* _a2dpHandle = nullptr;
};
