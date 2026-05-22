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

bool AudioManager::begin(const Config& cfg, SemaphoreHandle_t sdMutex, bool& sdReady, LogFn logFn) {
    _cfg = cfg;
    _sdMutex = sdMutex;
    _sdReady = &sdReady;
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

    logf(LOG_INFO, "AUDIO", "I2S task core=%d prio=%u stack=%lu | BCLK=%d LRC=%d DOUT=%d",
         (int)_cfg.taskCore, (unsigned)_cfg.taskPriority, (unsigned long)_cfg.taskStackWords,
         _cfg.pins.bclk, _cfg.pins.lrc, _cfg.pins.dout);
    return true;
}

void AudioManager::setVolume(uint8_t volume) {
    if (volume < _cfg.minVolume) volume = _cfg.minVolume;
    if (volume > _cfg.maxVolume) volume = _cfg.maxVolume;
    _audio.setVolume(volume);
}

bool AudioManager::takeSdMutex() {
    if (_sdMutex == nullptr) return true;
    return xSemaphoreTake(_sdMutex, _cfg.sdMutexTimeout) == pdTRUE;
}

void AudioManager::giveSdMutex() {
    if (_sdMutex != nullptr) xSemaphoreGive(_sdMutex);
}

bool AudioManager::sdFileExists(const char* path) {
    if (!_sdReady || !(*_sdReady) || !path) return false;
    if (!takeSdMutex()) {
        logf(LOG_WARN, "AUDIO", "SD mutex timeout (exists check): %s", path);
        return false;
    }
    File f = SD.open(path, FILE_READ);
    bool ok = (bool)f;
    if (f) f.close();
    giveSdMutex();
    return ok;
}

uint32_t AudioManager::sdFileSize(const char* path) {
    if (!_sdReady || !(*_sdReady) || !path) return 0;
    if (!takeSdMutex()) return 0;
    File f = SD.open(path, FILE_READ);
    uint32_t sz = f ? f.size() : 0;
    if (f) f.close();
    giveSdMutex();
    return sz;
}

bool AudioManager::playFromSd(const char* path) {
    if (!path || !path[0]) {
        logf(LOG_ERROR, "AUDIO", "Invalid path");
        return false;
    }
    if (!_sdReady || !(*_sdReady)) {
        logf(LOG_ERROR, "AUDIO", "SD not ready");
        return false;
    }
    if (!sdFileExists(path)) {
        logf(LOG_ERROR, "AUDIO", "File not found: %s", path);
        return false;
    }
    uint32_t sz = sdFileSize(path);
    if (sz == 0) {
        logf(LOG_ERROR, "AUDIO", "File empty: %s", path);
        return false;
    }

    if (!takeSdMutex()) {
        logf(LOG_WARN, "AUDIO", "SD mutex timeout — skip play: %s", path);
        return false;
    }
    bool ok = _audio.connecttoFS(SD, path);
    giveSdMutex();

    _playing = ok;
    if (ok) {
        logf(LOG_INFO, "AUDIO", "Playing %s (%lu bytes)", path, (unsigned long)sz);
    } else {
        logf(LOG_ERROR, "AUDIO", "connecttoFS failed: %s", path);
    }
    return ok;
}

void AudioManager::stop() {
    _audio.stopSong();
    _playing = false;
}

bool AudioManager::isRunning() {
    return _audio.isRunning();
}

void AudioManager::taskEntry(void* arg) {
    static_cast<AudioManager*>(arg)->taskLoop();
}

void AudioManager::taskLoop() {
    logf(LOG_DEBUG, "AUDIO", "I2S pump task running (decode+SD read inside library)");
    uint32_t idleLogAt = 0;
    while (true) {
        _audio.loop();
        if (_playing && !_audio.isRunning()) {
            _playing = false;
        }
        vTaskDelay(pdMS_TO_TICKS(_cfg.loopDelayMs));
        if (_playing && millis() - idleLogAt > 5000) {
            idleLogAt = millis();
            logf(LOG_DEBUG, "AUDIO", "Pump active, isRunning=%d", _audio.isRunning() ? 1 : 0);
        }
    }
}
