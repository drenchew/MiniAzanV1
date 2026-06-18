#pragma once

#include <Arduino.h>
#include <Audio.h>
#include "StorageManager.h"
#include "freertos/queue.h"

// Non-blocking I2S playback (ESP32-audioI2S). All play/stop runs on AudioTask only.
class AudioManager {
public:
    struct Pins {
        int bclk;
        int lrc;
        int dout;
    };

    struct Config {
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
    using MetadataFn = void (*)(const char* title, void* user);

    bool begin(StorageManager& storage, const Config& cfg, LogFn logFn = nullptr);
    void setMetadataCallback(MetadataFn fn, void* user = nullptr);
    void setVolume(uint8_t volume);

    /** Queue play (any task) — executed on AudioTask only. */
    bool requestPlay(const char* path);
    bool requestStop();
    bool requestPause();
    bool requestResume();
    /** P0 fast lane: flush pending plays, wake AudioTask immediately. */
    bool requestEmergencyStop();
    bool requestSetVolume(uint8_t volume);

    bool isRunning();
    bool isPlayingFlag() const { return _playing; }
    bool isPausedFlag() const { return _paused; }

    Audio& library() { return _audio; }

private:
    enum class CmdType : uint8_t { Play = 0, Stop, Pause, Resume, SetVolume };
    static constexpr size_t kAudioPathMax = 96;

    struct Command {
        CmdType type = CmdType::Stop;
        char path[kAudioPathMax]{};
        uint8_t volume = 0;
    };

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
    Audio _audio;

    QueueHandle_t _cmdQ = nullptr;
    TaskHandle_t _task = nullptr;
    MetadataFn _metaCb = nullptr;
    void* _metaUser = nullptr;
    volatile bool _playing = false;
    volatile bool _paused = false;
};
