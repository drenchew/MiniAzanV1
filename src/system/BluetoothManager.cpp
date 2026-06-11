#include "system/BluetoothManager.h"
#include "system/AzanSafeMode.h"
#include "AppLog.h"
#include "BluetoothA2DPSink.h"

bool BluetoothManager::begin(const Config& cfg) {
    _cfg = cfg;
    _enabled = false;
    _connected = false;
    _streamingEnabled = false;
    _streamingActive = false;
    _a2dpStarted = false;
    _streamVolumePct = cfg.defaultStreamVolume > 100 ? 100 : cfg.defaultStreamVolume;
    _job = {};
    _job.phase = BtTransferPhase::Idle;
    appLog(APP_LOG_INFO, "BT", "BluetoothManager ready (A2DP sink idle, SPP transfer not implemented)");
    return true;
}

void BluetoothManager::poll() {
    if (_job.phase == BtTransferPhase::Receiving && AzanSafeMode::isActive()) {
        _job.phase = BtTransferPhase::PausedForAzan;
        appLog(APP_LOG_WARN, "BT", "transfer paused — azan playing");
        emitProgress();
    }
    if (_streamingEnabled && AzanSafeMode::isActive()) {
        appLog(APP_LOG_WARN, "BT", "streaming stopped — azan playing");
        requestStreamingMode(false);
    }
}

bool BluetoothManager::isTransferActive() const {
    return _job.phase == BtTransferPhase::Receiving
        || _job.phase == BtTransferPhase::Writing
        || _job.phase == BtTransferPhase::QueuedForSd;
}

bool BluetoothManager::requestTransferMode(bool on) {
    if (on && _streamingEnabled) {
        appLog(APP_LOG_WARN, "BT", "transfer mode blocked while A2DP streaming is enabled");
        return false;
    }
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

bool BluetoothManager::requestStreamingMode(bool on) {
    if (on) {
        if (AzanSafeMode::isActive()) {
            appLog(APP_LOG_WARN, "BT", "A2DP streaming blocked during azan");
            return false;
        }
        if (isTransferActive()) {
            appLog(APP_LOG_WARN, "BT", "A2DP streaming blocked while transfer is active");
            return false;
        }
        if (_a2dpStarted) {
            _streamingEnabled = true;
            _enabled = true;
            emitProgress();
            return true;
        }
        if (!_a2dpSink) {
            _a2dpSink = new BluetoothA2DPSink();
            if (!_a2dpSink) {
                appLog(APP_LOG_ERROR, "BT", "A2DP sink allocation failed");
                return false;
            }
        }

        i2s_pin_config_t pins{};
        pins.mck_io_num = I2S_PIN_NO_CHANGE;
        pins.bck_io_num = _cfg.pins.bclk;
        pins.ws_io_num = _cfg.pins.lrc;
        pins.data_out_num = _cfg.pins.dout;
        pins.data_in_num = I2S_PIN_NO_CHANGE;

        _a2dpSink->set_i2s_port(I2S_NUM_1);
        _a2dpSink->set_pin_config(pins);
        _a2dpSink->set_on_connection_state_changed(connectionStateThunk, this);
        _a2dpSink->set_on_audio_state_changed(audioStateThunk, this);
        _a2dpSink->set_volume(toA2dpVolume(_streamVolumePct));
        _a2dpSink->start(_cfg.deviceName ? _cfg.deviceName : "MiniAzan Speaker");

        _a2dpStarted = true;
        _streamingEnabled = true;
        _enabled = true;
        _job.phase = BtTransferPhase::AwaitingConnection;
        appLog(APP_LOG_INFO, "BT", "A2DP streaming ON (I2S_NUM_1)");
        emitProgress();
        return true;
    }

    if (_a2dpStarted && _a2dpSink) {
        _a2dpSink->end(false);
    }
    _a2dpStarted = false;
    _streamingEnabled = false;
    _streamingActive = false;
    _connected = false;
    _enabled = false;
    _job.phase = BtTransferPhase::Idle;
    appLog(APP_LOG_INFO, "BT", "A2DP streaming OFF");
    emitProgress();
    return true;
}

bool BluetoothManager::requestSetStreamVolume(uint8_t pct) {
    if (pct > 100) {
        pct = 100;
    }
    _streamVolumePct = pct;
    if (_a2dpStarted && _a2dpSink) {
        _a2dpSink->set_volume(toA2dpVolume(pct));
    }
    emitProgress();
    return true;
}

void BluetoothManager::setProgressCallback(ProgressFn fn, void* user) {
    _progressFn = fn;
    _progressUser = user;
}

void BluetoothManager::connectionStateThunk(esp_a2d_connection_state_t state, void* user) {
    if (user) {
        static_cast<BluetoothManager*>(user)->onConnectionState(state);
    }
}

void BluetoothManager::audioStateThunk(esp_a2d_audio_state_t state, void* user) {
    if (user) {
        static_cast<BluetoothManager*>(user)->onAudioState(state);
    }
}

void BluetoothManager::onConnectionState(esp_a2d_connection_state_t state) {
    _connected = (state == ESP_A2D_CONNECTION_STATE_CONNECTED);
    if (!_connected) {
        _streamingActive = false;
    }
    if (_streamingEnabled) {
        _job.phase = _connected ? BtTransferPhase::Receiving : BtTransferPhase::AwaitingConnection;
    }
    appLogf(APP_LOG_INFO, "BT", "A2DP connection state=%d", (int)state);
    emitProgress();
}

void BluetoothManager::onAudioState(esp_a2d_audio_state_t state) {
    _streamingActive = (state == ESP_A2D_AUDIO_STATE_STARTED);
    appLogf(APP_LOG_INFO, "BT", "A2DP audio state=%d", (int)state);
    emitProgress();
}

void BluetoothManager::emitProgress() {
    if (_progressFn) {
        _progressFn(_job, _progressUser);
    }
}

uint8_t BluetoothManager::toA2dpVolume(uint8_t pct) const {
    if (pct > 100) {
        pct = 100;
    }
    return (uint8_t)((pct * 127U + 50U) / 100U);
}
