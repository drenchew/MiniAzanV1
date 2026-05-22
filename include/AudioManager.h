#pragma once

#include <Arduino.h>
#include <Audio.h>
#include "StorageManager.h"

// Non-blocking I2S playback (ESP32-audioI2S). SD access only via StorageManager.
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

    bool begin(StorageManager& storage, const Config& cfg, LogFn logFn = nullptr);
    void setVolume(uint8_t volume);

    bool playFromSd(const char* path);
    void stop();
    bool isRunning();

    Audio& library() { return _audio; }

private:
    static void taskEntry(void* arg);
    void taskLoop();
    void logf(int level, const char* tag, const char* fmt, ...) const;

    Config _cfg{};
    LogFn _log = nullptr;
    StorageManager* _storage = nullptr;
    Audio _audio;

    TaskHandle_t _task = nullptr;
    volatile bool _playing = false;
};
