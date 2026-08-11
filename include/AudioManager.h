#pragma once

#include <Arduino.h>
#include <Audio.h>
#include <driver/i2s.h>
#include "StorageManager.h"
#include "BoardConfig.h"
#include "freertos/queue.h"

// MP3 playback via ESP32-audioI2S. Output: internal DAC (AUX) or external I2S.
class AudioManager {
public:
    enum class OutputMode : uint8_t {
        I2sExternal = 0,
        InternalDacAux,
    };

    struct Pins {
        int bclk = -1;
        int lrc = -1;
        int dout = -1;
    };

    struct Config {
        OutputMode outputMode = BoardConfig::kAudioAuxDac ? OutputMode::InternalDacAux
                                                          : OutputMode::I2sExternal;
        Pins pins{};
        uint8_t defaultVolume = 9;
        uint8_t minVolume = 0;
        uint8_t maxVolume = 21;
        UBaseType_t taskPriority = 5;
        uint32_t taskStackWords = 16384;
        BaseType_t taskCore = 0;
        uint32_t loopDelayMs = 1;
    };

    using LogFn = void (*)(int level, const char* tag, const char* message);

    AudioManager() = default;
    ~AudioManager();

    bool begin(StorageManager& storage, const Config& cfg, LogFn logFn = nullptr);
    void setVolume(uint8_t volume);

    bool requestPlay(const char* path);
    bool requestStop();
    bool requestPause();
    bool requestResume();
    bool requestEmergencyStop();
    bool requestSetVolume(uint8_t volume);

    bool isRunning();
    bool isPlayingFlag() const { return _playing; }
    bool isPausedFlag() const { return _paused; }
    OutputMode outputMode() const { return _cfg.outputMode; }

    Audio& library() { return *_audio; }

private:
    enum class CmdType : uint8_t { Play = 0, Stop, Pause, Resume, SetVolume };
    static constexpr size_t kAudioPathMax = 96;

    struct Command {
        CmdType type = CmdType::Stop;
        char path[kAudioPathMax]{};
        uint8_t volume = 0;
    };

    bool createAudioInstance();
    void destroyAudioInstance();

    static void taskEntry(void* arg);
    void taskLoop();
    void drainCommands();
    bool playFromSdInternal(const char* path);
    void stopInternal();
    void pauseInternal();
    void resumeInternal();
    void logf(int level, const char* tag, const char* fmt, ...) const;

    Config _cfg{};
    LogFn _log = nullptr;
    StorageManager* _storage = nullptr;
    alignas(Audio) uint8_t _audioStorage[sizeof(Audio)]{};
    Audio* _audio = nullptr;

    QueueHandle_t _cmdQ = nullptr;
    TaskHandle_t _task = nullptr;
    volatile bool _playing = false;
    volatile bool _paused = false;
};
