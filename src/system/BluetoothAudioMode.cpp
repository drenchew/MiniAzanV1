/**
 * @file  BluetoothAudioMode.cpp
 * @brief A2DP Bluetooth audio streaming implementation.
 *
 * INTEGRATION WITH EXISTING SYSTEM
 * ────────────────────────────────
 * This module integrates with:
 *   • AzanSafeMode — pauses streaming during azan playback
 *   • AudioManager — shares I2S pins (no conflict: only one active)
 *   • MemoryManager — audio buffers allocated at boot
 *
 * A2DP CONNECTION FLOW
 * ────────────────────
 *  1. User enables streaming mode via requestStreamingMode(true)
 *  2. ESP32 becomes discoverable as "MiniAzan" Bluetooth speaker
 *  3. Phone connects and streams audio (A2DP protocol)
 *  4. Audio frames received and queued for I2S playback
 *  5. During azan: BluetoothAudioMode pauses (pauseForAzan)
 *  6. After azan: BluetoothAudioMode resumes (resumeAfterAzan)
 *  7. User disables: graceful disconnect, BT disabled
 */

#include "system/BluetoothAudioMode.h"
#include "system/AzanSafeMode.h"
#include "AppLog.h"
#include <esp_heap_caps.h>
#include <cstring>
#include <cstdarg>
#include <BluetoothA2DPSink.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Global instance (created on demand when streaming is enabled)
// ─────────────────────────────────────────────────────────────────────────────
static BluetoothA2DPSink* g_a2dp_sink = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
//  BluetoothAudioMode implementation
// ─────────────────────────────────────────────────────────────────────────────

bool BluetoothAudioMode::begin(const Config& cfg, LogFn logFn) {
    _cfg = cfg;
    _log = logFn;

    logf(APP_LOG_INFO, "BTAUDIO", "Initializing Bluetooth A2DP audio mode...");

    // Pre-allocate audio buffers from heap
    logf(APP_LOG_INFO, "BTAUDIO", "Allocating %u audio buffers (%u B each)",
         _cfg.numAudioBuffers, _cfg.audioBufferSizeBytes);

    _audioBufferSize = _cfg.audioBufferSizeBytes;
    for (uint8_t i = 0; i < _cfg.numAudioBuffers; ++i) {
        _audioBuffers[i] = static_cast<uint8_t*>(
            heap_caps_malloc(_audioBufferSize, MALLOC_CAP_DEFAULT)
        );
        if (!_audioBuffers[i]) {
            logf(APP_LOG_ERROR, "BTAUDIO",
                 "FATAL: Failed to allocate audio buffer %u", i);
            // Cleanup previous allocations
            for (uint8_t j = 0; j < i; ++j) {
                if (_audioBuffers[j]) {
                    heap_caps_free(_audioBuffers[j]);
                    _audioBuffers[j] = nullptr;
                }
            }
            return false;
        }
    }

    // Create command queue
    _cmdQ = xQueueCreate(8, sizeof(Command));
    if (!_cmdQ) {
        logf(APP_LOG_ERROR, "BTAUDIO", "Failed to create command queue");
        return false;
    }

    // Initialize A2DP stack (placeholder — actual implementation below)
    if (!initA2dp()) {
        logf(APP_LOG_ERROR, "BTAUDIO", "Failed to initialize A2DP stack");
        vQueueDelete(_cmdQ);
        _cmdQ = nullptr;
        return false;
    }

    // Create task
    BaseType_t ret = xTaskCreatePinnedToCore(
        taskEntry,
        "BtAudioTask",
        _cfg.taskStackWords,
        this,
        _cfg.taskPriority,
        &_task,
        _cfg.taskCore
    );
    if (ret != pdPASS) {
        logf(APP_LOG_ERROR, "BTAUDIO", "Failed to create Bluetooth audio task");
        vQueueDelete(_cmdQ);
        _cmdQ = nullptr;
        return false;
    }

    logf(APP_LOG_INFO, "BTAUDIO", "✓ Bluetooth A2DP mode ready (disabled until requested)");
    return true;
}

bool BluetoothAudioMode::requestStreamingMode(bool enable) {
    Command cmd{};
    cmd.type = enable ? CmdType::EnableStreaming : CmdType::DisableStreaming;

    if (!_cmdQ) return false;
    return xQueueSend(_cmdQ, &cmd, pdMS_TO_TICKS(100)) == pdPASS;
}

void BluetoothAudioMode::setVolume(uint8_t volume) {
    _volume = (volume > 100) ? 100 : volume;

    Command cmd{};
    cmd.type = CmdType::SetVolume;
    cmd.volume = _volume;

    if (_cmdQ) {
        xQueueSend(_cmdQ, &cmd, 0);
    }
}

void BluetoothAudioMode::poll() {
    // Called from main loop or coordinator; just updates state flags
    // (actual work happens in taskLoop on dedicated task)
}

void BluetoothAudioMode::pauseForAzan() {
    if (!_streamingActive || _pausedForAzan) return;

    Command cmd{};
    cmd.type = CmdType::PauseForAzan;

    if (_cmdQ) {
        xQueueSend(_cmdQ, &cmd, 0);
    }
    _pausedForAzan = true;

    logf(APP_LOG_INFO, "BTAUDIO", "BT streaming paused for azan");
}

void BluetoothAudioMode::resumeAfterAzan() {
    if (!_streamingActive || !_pausedForAzan) return;

    Command cmd{};
    cmd.type = CmdType::ResumeAfterAzan;

    if (_cmdQ) {
        xQueueSend(_cmdQ, &cmd, 0);
    }
    _pausedForAzan = false;

    logf(APP_LOG_INFO, "BTAUDIO", "BT streaming resumed after azan");
}

void BluetoothAudioMode::setConnectionCallback(ConnectionCallback cb, void* user) {
    _connCb = cb;
    _connUser = user;
}

void BluetoothAudioMode::setPlaybackCallback(PlaybackCallback cb, void* user) {
    _playbackCb = cb;
    _playbackUser = user;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Private implementation
// ─────────────────────────────────────────────────────────────────────────────

void BluetoothAudioMode::taskEntry(void* arg) {
    BluetoothAudioMode* self = static_cast<BluetoothAudioMode*>(arg);
    self->taskLoop();
}

void BluetoothAudioMode::taskLoop() {
    logf(APP_LOG_INFO, "BTAUDIO", "Audio task loop started");

    while (true) {
        // Drain any pending commands
        drainCommands();

        // Poll A2DP state (placeholder)
        // In real implementation: check connection, receive audio frames, send to I2S

        // Check if we should pause (azan active)
        if (_streamingActive && !_pausedForAzan && AzanSafeMode::isActive()) {
            pauseForAzan();
        }

        // Sleep to yield to other tasks
        vTaskDelay(pdMS_TO_TICKS(_cfg.loopDelayMs));
    }
}

void BluetoothAudioMode::drainCommands() {
    Command cmd{};
    while (xQueueReceive(_cmdQ, &cmd, 0) == pdPASS) {
        switch (cmd.type) {
        case CmdType::Stop:
            break;

        case CmdType::SetVolume:
            logf(APP_LOG_DEBUG, "BTAUDIO", "Volume set to %u%%", cmd.volume);
            break;

        case CmdType::EnableStreaming: {
            _streamingEnabled = true;
            logf(APP_LOG_INFO, "BTAUDIO", "A2DP streaming mode enabled");
            
            // Create A2DP sink on first use (lazy initialization)
            if (!g_a2dp_sink) {
                g_a2dp_sink = new BluetoothA2DPSink();
                
                // Configure I2S pins
                i2s_pin_config_t pin_config = {
                    .bck_io_num = 26,      // GPIO26 = BCLK
                    .ws_io_num = 25,       // GPIO25 = LRC
                    .data_out_num = 27,    // GPIO27 = DOUT
                    .data_in_num = -1      // Not used for output
                };
                g_a2dp_sink->set_pin_config(pin_config);
            }
            
            // Start the A2DP sink (this makes device discoverable)
            g_a2dp_sink->start("MiniAzan Speaker");
            logf(APP_LOG_INFO, "BTAUDIO", "Device is now discoverable as 'MiniAzan Speaker'");
            _deviceConnected = false;
            _streamingActive = false;
            break;
        }

        case CmdType::DisableStreaming: {
            _streamingEnabled = false;
            _streamingActive = false;
            logf(APP_LOG_INFO, "BTAUDIO", "A2DP streaming mode disabled");
            
            // Stop the A2DP sink but keep it allocated
            if (g_a2dp_sink) {
                g_a2dp_sink->end();
            }
            break;
        }

        case CmdType::PauseForAzan:
            logf(APP_LOG_DEBUG, "BTAUDIO", "Pausing for azan...");
            // In real implementation: pause I2S DMA, queue pause command to A2DP
            break;

        case CmdType::ResumeAfterAzan:
            logf(APP_LOG_DEBUG, "BTAUDIO", "Resuming after azan...");
            // In real implementation: resume I2S DMA, queue resume command to A2DP
            break;

        default:
            break;
        }
    }
}

bool BluetoothAudioMode::initA2dp() {
    logf(APP_LOG_INFO, "BTAUDIO", "A2DP stack ready (will initialize on first use)");
    return true;
}

void BluetoothAudioMode::setupI2sForStreaming() {
    logf(APP_LOG_INFO, "BTAUDIO", "Setting up I2S for audio streaming...");

    // TODO: Configure I2S mode (shared with AudioManager)
    // Ensure I2S is not in use by AudioManager before initializing
}

void BluetoothAudioMode::cleanupA2dp() {
    logf(APP_LOG_INFO, "BTAUDIO", "Cleaning up A2DP resources...");

    // TODO: Implement cleanup
    // 1. Disable A2DP
    // 2. Stop I2S
    // 3. Disconnect Bluetooth
}

void BluetoothAudioMode::logf(int level, const char* tag, const char* fmt, ...) const {
    if (!_log) return;

    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    _log(level, tag, buffer);
}
