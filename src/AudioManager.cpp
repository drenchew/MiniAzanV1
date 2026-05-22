#include "AudioManager.h"

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

    logf(LOG_INFO, "AUDIO", "I2S task core=%d prio=%u | VSPI SD via StorageManager",
         (int)_cfg.taskCore, (unsigned)_cfg.taskPriority);
    return true;
}

void AudioManager::setVolume(uint8_t volume) {
    if (volume < _cfg.minVolume) volume = _cfg.minVolume;
    if (volume > _cfg.maxVolume) volume = _cfg.maxVolume;
    _audio.setVolume(volume);
}

bool AudioManager::playFromSd(const char* path) {
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
    uint32_t sz = _storage->fileSize(path);
    if (sz == 0) {
        logf(LOG_ERROR, "AUDIO", "File empty: %s", path);
        return false;
    }

    _storage->setPlaybackLocked(true);
    bool ok = _audio.connecttoFS(_storage->mediaFs(), path);
    if (!ok) {
        _storage->setPlaybackLocked(false);
        logf(LOG_ERROR, "AUDIO", "connecttoFS failed: %s", path);
        return false;
    }

    _playing = true;
    logf(LOG_INFO, "AUDIO", "Playing %s (%lu bytes)", path, (unsigned long)sz);
    return true;
}

void AudioManager::stop() {
    _audio.stopSong();
    _playing = false;
    if (_storage) _storage->setPlaybackLocked(false);
}

bool AudioManager::isRunning() {
    return _audio.isRunning();
}

void AudioManager::taskEntry(void* arg) {
    static_cast<AudioManager*>(arg)->taskLoop();
}

void AudioManager::taskLoop() {
    logf(LOG_DEBUG, "AUDIO", "I2S pump (library reads VSPI SD during decode)");
    while (true) {
        _audio.loop();
        if (_playing && !_audio.isRunning()) {
            _playing = false;
            if (_storage) _storage->setPlaybackLocked(false);
        }
        vTaskDelay(pdMS_TO_TICKS(_cfg.loopDelayMs));
    }
}
