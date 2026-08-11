#include "AudioManager.h"
#include "system/AzanSafeMode.h"
#include "system/MemoryGuard.h"
#include <new>
#include <stdarg.h>

#ifndef LOG_ERROR
#define LOG_ERROR 0
#define LOG_WARN  1
#define LOG_INFO  2
#define LOG_DEBUG 3
#endif

namespace {

constexpr int kAudioInputBufferRamBytes = 8192;
constexpr int kAudioInputBufferPsramBytes = 0;

template<int N>
struct AudioBufferPriority : AudioBufferPriority<N - 1> {};

template<>
struct AudioBufferPriority<0> {};

template<typename T>
auto configureAudioInputBuffer(T& audio, AudioBufferPriority<3>)
    -> decltype(audio.setBufsize(kAudioInputBufferRamBytes, kAudioInputBufferPsramBytes), bool()) {
    audio.setBufsize(kAudioInputBufferRamBytes, kAudioInputBufferPsramBytes);
    return true;
}

template<typename T>
auto configureAudioInputBuffer(T& audio, AudioBufferPriority<2>)
    -> decltype(audio.setBufsize((size_t)kAudioInputBufferRamBytes), bool()) {
    audio.setBufsize((size_t)kAudioInputBufferRamBytes);
    return true;
}

template<typename T>
auto configureAudioInputBuffer(T& audio, AudioBufferPriority<1>)
    -> decltype(audio.setBufferSize((size_t)kAudioInputBufferRamBytes), bool()) {
    audio.setBufferSize((size_t)kAudioInputBufferRamBytes);
    return true;
}

template<typename T>
bool configureAudioInputBuffer(T&, AudioBufferPriority<0>) {
    return false;
}

}  // namespace

AudioManager::~AudioManager() {
    destroyAudioInstance();
}

bool AudioManager::createAudioInstance() {
    destroyAudioInstance();

    if (_cfg.outputMode == OutputMode::InternalDacAux) {
#if defined(CONFIG_IDF_TARGET_ESP32)
        _audio = new (_audioStorage) Audio(true, I2S_DAC_CHANNEL_RIGHT_EN);
        logf(LOG_INFO, "AUDIO", "Output: internal DAC GPIO%d (AUX tip)", MINI_AZAN_AUX_DAC_GPIO);
        return true;
#else
        logf(LOG_ERROR, "AUDIO", "Internal DAC not available on this chip (use I2S DAC module)");
        return false;
#endif
    }

    _audio = new (_audioStorage) Audio(false);
    if (_cfg.pins.bclk < 0 || _cfg.pins.lrc < 0 || _cfg.pins.dout < 0) {
        logf(LOG_ERROR, "AUDIO", "I2S pins not configured");
        destroyAudioInstance();
        return false;
    }
    if (!_audio->setPinout((uint8_t)_cfg.pins.bclk, (uint8_t)_cfg.pins.lrc, (uint8_t)_cfg.pins.dout)) {
        logf(LOG_ERROR, "AUDIO", "setPinout failed BCLK=%d LRC=%d DOUT=%d",
             _cfg.pins.bclk, _cfg.pins.lrc, _cfg.pins.dout);
        destroyAudioInstance();
        return false;
    }
    logf(LOG_INFO, "AUDIO", "Output: I2S BCLK=%d LRC=%d DOUT=%d",
         _cfg.pins.bclk, _cfg.pins.lrc, _cfg.pins.dout);
    return true;
}

void AudioManager::destroyAudioInstance() {
    if (_audio) {
        _audio->~Audio();
        _audio = nullptr;
    }
}

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

    if (_cfg.outputMode == OutputMode::I2sExternal && _cfg.pins.bclk < 0) {
        _cfg.pins.bclk = BoardConfig::I2S_BCLK;
        _cfg.pins.lrc = BoardConfig::I2S_LRC;
        _cfg.pins.dout = BoardConfig::I2S_DOUT;
    }

    if (!_cmdQ) {
        _cmdQ = xQueueCreate(4, sizeof(Command));
    }
    if (!_cmdQ) {
        return false;
    }

    if (!createAudioInstance()) {
        return false;
    }

    const bool inputBufferConfigured =
        configureAudioInputBuffer(*_audio, AudioBufferPriority<3>{});
    _audio->setVolume(_cfg.defaultVolume);
    logf(LOG_INFO, "AUDIO", "Input buffer %s at %d bytes",
         inputBufferConfigured ? "capped" : "default",
         kAudioInputBufferRamBytes);

    BaseType_t ok = xTaskCreatePinnedToCore(
        taskEntry,
        "AudioOut",
        _cfg.taskStackWords,
        this,
        _cfg.taskPriority,
        &_task,
        _cfg.taskCore);

    if (ok != pdPASS) {
        logf(LOG_ERROR, "AUDIO", "Failed to create audio task");
        destroyAudioInstance();
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

bool AudioManager::requestPause() {
    if (!_cmdQ) {
        return false;
    }
    Command c{};
    c.type = CmdType::Pause;
    return xQueueSend(_cmdQ, &c, 0) == pdTRUE;
}

bool AudioManager::requestResume() {
    if (!_cmdQ) {
        return false;
    }
    Command c{};
    c.type = CmdType::Resume;
    return xQueueSend(_cmdQ, &c, 0) == pdTRUE;
}

bool AudioManager::requestEmergencyStop() {
    if (!_cmdQ) {
        return false;
    }
    Command dummy{};
    while (xQueueReceive(_cmdQ, &dummy, 0) == pdTRUE) {
    }
    Command c{};
    c.type = CmdType::Stop;
    const bool ok = xQueueSend(_cmdQ, &c, 0) == pdTRUE;
    if (_task) {
        xTaskNotifyGive(_task);
    }
    return ok;
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
    if (!_audio) return;
    Command c{};
    while (_cmdQ && xQueueReceive(_cmdQ, &c, 0) == pdTRUE) {
        switch (c.type) {
            case CmdType::Play:
                playFromSdInternal(c.path);
                break;
            case CmdType::Stop:
                stopInternal();
                break;
            case CmdType::Pause:
                pauseInternal();
                break;
            case CmdType::Resume:
                resumeInternal();
                break;
            case CmdType::SetVolume: {
                uint8_t v = c.volume;
                if (v < _cfg.minVolume) v = _cfg.minVolume;
                if (v > _cfg.maxVolume) v = _cfg.maxVolume;
                _audio->setVolume(v);
                break;
            }
            default:
                break;
        }
    }
}

bool AudioManager::playFromSdInternal(const char* path) {
    if (!_audio || !_storage || !path || !path[0]) {
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

    const bool ok = _audio->connecttoFS(_storage->mediaFs(), path);
    if (!ok) {
        _storage->setPlaybackLocked(false);
        AzanSafeMode::exit();
        logf(LOG_ERROR, "AUDIO", "connecttoFS failed: %s", path);
        return false;
    }

    _playing = true;
    _paused = false;
    logf(LOG_INFO, "AUDIO", "Playing %s (%lu bytes)", path, (unsigned long)sz);
    return true;
}

void AudioManager::stopInternal() {
    if (!_audio) return;
    _audio->stopSong();
    _playing = false;
    _paused = false;
    if (_storage) {
        _storage->setPlaybackLocked(false);
    }
    if (AzanSafeMode::isActive()) {
        AzanSafeMode::exit();
    }
}

void AudioManager::pauseInternal() {
    if (!_audio || !_playing || _paused) {
        return;
    }
    if (_audio->pauseResume()) {
        _paused = true;
        logf(LOG_INFO, "AUDIO", "Paused");
    }
}

void AudioManager::resumeInternal() {
    if (!_audio || !_playing || !_paused) {
        return;
    }
    if (_audio->pauseResume()) {
        _paused = false;
        logf(LOG_INFO, "AUDIO", "Resumed");
    }
}

bool AudioManager::isRunning() {
    return _audio && _audio->isRunning();
}

void AudioManager::taskEntry(void* arg) {
    static_cast<AudioManager*>(arg)->taskLoop();
}

void AudioManager::taskLoop() {
    logf(LOG_DEBUG, "AUDIO", "audio pump task running");
    while (true) {
        ulTaskNotifyTake(pdTRUE, 0);
        drainCommands();
        if (_audio) {
            _audio->loop();
        }
        if (_audio && _playing && !_paused && !_audio->isRunning()) {
            _playing = false;
            _paused = false;
            if (_storage) {
                _storage->setPlaybackLocked(false);
            }
            AzanSafeMode::exit();
            logf(LOG_INFO, "AUDIO", "Playback finished");
        }
        vTaskDelay(pdMS_TO_TICKS(_cfg.loopDelayMs));
    }
}
