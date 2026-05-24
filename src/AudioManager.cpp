#include "AudioManager.h"
#include "system/AzanSafeMode.h"
#include "system/MemoryGuard.h"
#include <stdarg.h>

#ifndef LOG_ERROR
#define LOG_ERROR 0
#define LOG_WARN  1
#define LOG_INFO  2
#define LOG_DEBUG 3
#endif

void AudioManager::logf(int level, const char* tag, const char* fmt, ...) const {
    if (!_log) return;
    char buf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    _log(level, tag, buf);
}

bool AudioManager::begin(StorageManager& storage, const Config& cfg, LogFn logFn) {
    _cfg = cfg;
    _storage = &storage;
    _log = logFn;

    if (!_cmdQ) {
        _cmdQ = xQueueCreate(4, sizeof(Command));
    }
    if (!_cmdQ) {
        return false;
    }

    _audio.setPinout(_cfg.pins.bclk, _cfg.pins.lrc, _cfg.pins.dout);
    _audio.setVolume(_cfg.defaultVolume);

    BaseType_t ok = xTaskCreatePinnedToCore(
        taskEntry,
        "AudioI2S",
        _cfg.taskStackWords,
        this,
        _cfg.taskPriority,
        &_task,
        _cfg.taskCore);

    if (ok != pdPASS) {
        logf(LOG_ERROR, "AUDIO", "Failed to create I2S task");
        return false;
    }

    logf(LOG_INFO, "AUDIO", "AudioTask core=%d prio=%u (queue-driven)",
         (int)_cfg.taskCore, (unsigned)_cfg.taskPriority);
    return true;
}

bool AudioManager::requestPlay(const char* path) {
    if (!_cmdQ || !path || !path[0]) {
        return false;
    }
    Command c{};
    c.type = CmdType::Play;
    strncpy(c.path, path, sizeof(c.path) - 1);
    return xQueueSend(_cmdQ, &c, 0) == pdTRUE;
}

bool AudioManager::requestStop() {
    if (!_cmdQ) {
        return false;
    }
    Command c{};
    c.type = CmdType::Stop;
    return xQueueSend(_cmdQ, &c, 0) == pdTRUE;
}

bool AudioManager::requestSetVolume(uint8_t volume) {
    if (!_cmdQ) {
        return false;
    }
    Command c{};
    c.type = CmdType::SetVolume;
    c.volume = volume;
    return xQueueSend(_cmdQ, &c, 0) == pdTRUE;
}

void AudioManager::setVolume(uint8_t volume) {
    requestSetVolume(volume);
}

void AudioManager::drainCommands() {
    Command c{};
    while (_cmdQ && xQueueReceive(_cmdQ, &c, 0) == pdTRUE) {
        switch (c.type) {
            case CmdType::Play:
                playFromSdInternal(c.path);
                break;
            case CmdType::Stop:
                stopInternal();
                break;
            case CmdType::SetVolume: {
                uint8_t v = c.volume;
                if (v < _cfg.minVolume) v = _cfg.minVolume;
                if (v > _cfg.maxVolume) v = _cfg.maxVolume;
                _audio.setVolume(v);
                break;
            }
            default:
                break;
        }
    }
}

bool AudioManager::playFromSdInternal(const char* path) {
    if (!_storage || !path || !path[0]) {
        logf(LOG_ERROR, "AUDIO", "Invalid path or storage");
        return false;
    }
    if (!_storage->isReady()) {
        logf(LOG_ERROR, "AUDIO", "SD not ready");
        return false;
    }
    if (_storage->isPlaybackLocked()) {
        logf(LOG_WARN, "AUDIO", "SD busy (playback lock)");
        return false;
    }
    if (!_storage->fileExists(path)) {
        logf(LOG_ERROR, "AUDIO", "File not found: %s", path);
        return false;
    }
    const uint32_t sz = _storage->fileSize(path);
    if (sz == 0) {
        logf(LOG_ERROR, "AUDIO", "File empty: %s", path);
        return false;
    }

    if (!MemoryGuard::canStartMp3Decode(sz)) {
        MemoryGuard::logHeapStatus("AUDIO");
        logf(LOG_ERROR, "AUDIO", "Rejected play — insufficient heap for MP3");
        return false;
    }

    stopInternal();

    _storage->setPlaybackLocked(true);
    AzanSafeMode::enter(path);

    const bool ok = _audio.connecttoFS(_storage->mediaFs(), path);
    if (!ok) {
        _storage->setPlaybackLocked(false);
        AzanSafeMode::exit();
        logf(LOG_ERROR, "AUDIO", "connecttoFS failed: %s", path);
        return false;
    }

    _playing = true;
    logf(LOG_INFO, "AUDIO", "Playing %s (%lu bytes)", path, (unsigned long)sz);
    return true;
}

void AudioManager::stopInternal() {
    _audio.stopSong();
    _playing = false;
    if (_storage) {
        _storage->setPlaybackLocked(false);
    }
    if (AzanSafeMode::isActive()) {
        AzanSafeMode::exit();
    }
}

bool AudioManager::isRunning() {
    return _audio.isRunning();
}

void AudioManager::taskEntry(void* arg) {
    static_cast<AudioManager*>(arg)->taskLoop();
}

void AudioManager::taskLoop() {
    logf(LOG_DEBUG, "AUDIO", "I2S pump (VSPI SD decode only on this task)");
    while (true) {
        drainCommands();
        _audio.loop();
        if (_playing && !_audio.isRunning()) {
            _playing = false;
            if (_storage) {
                _storage->setPlaybackLocked(false);
            }
            AzanSafeMode::exit();
            logf(LOG_INFO, "AUDIO", "Playback finished");
        }
        vTaskDelay(pdMS_TO_TICKS(_cfg.loopDelayMs));
    }
}
