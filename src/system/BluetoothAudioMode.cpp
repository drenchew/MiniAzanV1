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
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Global Bluetooth state
// ─────────────────────────────────────────────────────────────────────────────
static bool g_bt_controller_initialized = false;

// GAP callback handler
static void esp_bt_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            // Auth complete
        }
        break;
    case ESP_BT_GAP_PIN_REQ_EVT: {
        uint8_t pin_code[] = {0x30, 0x30, 0x30, 0x30};  // "0000"
        esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        break;
    }
    case ESP_BT_GAP_CFM_REQ_EVT:
        // Handle confirmation request
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    default:
        break;
    }
}

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

    // Initialize A2DP stack (which initializes Bluetooth controller)
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
            // Device is already discoverable from boot
            _deviceConnected = false;
            _streamingActive = false;
            break;
        }

        case CmdType::DisableStreaming: {
            _streamingEnabled = false;
            _streamingActive = false;
            logf(APP_LOG_INFO, "BTAUDIO", "A2DP streaming mode disabled");
            // Keep device discoverable - user might want to pair again
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
    logf(APP_LOG_INFO, "BTAUDIO", "Initializing Bluetooth controller...");
    
    // 1. Initialize BT controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    bt_cfg.mode = ESP_BT_MODE_CLASSIC_BT;
    
    esp_err_t err = esp_bt_controller_init(&bt_cfg);
    if (err != ESP_OK) {
        logf(APP_LOG_ERROR, "BTAUDIO", "BT controller init failed: 0x%x", err);
        return false;
    }

    // 2. Enable BT controller
    err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK) {
        logf(APP_LOG_ERROR, "BTAUDIO", "BT controller enable failed: 0x%x", err);
        return false;
    }

    // 3. Initialize Bluedroid
    err = esp_bluedroid_init();
    if (err != ESP_OK) {
        logf(APP_LOG_ERROR, "BTAUDIO", "Bluedroid init failed: 0x%x", err);
        esp_bt_controller_disable();
        return false;
    }

    // 4. Enable Bluedroid
    err = esp_bluedroid_enable();
    if (err != ESP_OK) {
        logf(APP_LOG_ERROR, "BTAUDIO", "Bluedroid enable failed: 0x%x", err);
        esp_bluedroid_deinit();
        esp_bt_controller_disable();
        return false;
    }

    // 5. Register GAP callbacks (for handling pairing, etc.)
    err = esp_bt_gap_register_callback(esp_bt_gap_callback);
    if (err != ESP_OK) {
        logf(APP_LOG_ERROR, "BTAUDIO", "Failed to register GAP callback: 0x%x", err);
        return false;
    }

    // 6. Set Simple Secure Pairing mode
    uint8_t io_cap = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &io_cap, sizeof(io_cap));

    // 7. Make device discoverable immediately (for pairing)
    err = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    if (err != ESP_OK) {
        logf(APP_LOG_ERROR, "BTAUDIO", "Failed to set scan mode: 0x%x", err);
        return false;
    }
    logf(APP_LOG_INFO, "BTAUDIO", "Device is discoverable");

    g_bt_controller_initialized = true;
    logf(APP_LOG_INFO, "BTAUDIO", "✓ Bluetooth stack initialized and discoverable");
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
