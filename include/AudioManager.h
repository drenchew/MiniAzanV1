#pragma once

#include <Arduino.h>
#include <Audio.h>
#include <FS.h>
#include <SD.h>
#include "freertos/semphr.h"

// Non-blocking I2S playback wrapper around ESP32-audioI2S.
// SD access is serialized; decode+I2S runs on a dedicated FreeRTOS task.
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
        TickType_t sdMutexTimeout = pdMS_TO_TICKS(50);
    };

    using LogFn = void (*)(int level, const char* tag, const char* message);

    bool begin(const Config& cfg, SemaphoreHandle_t sdMutex, bool& sdReady, LogFn logFn = nullptr);
    void setVolume(uint8_t volume);

    bool playFromSd(const char* path);
    void stop();
    bool isRunning();

    Audio& library() { return _audio; }

private:
    static void taskEntry(void* arg);
    void taskLoop();

    bool sdFileExists(const char* path);
    uint32_t sdFileSize(const char* path);
    bool takeSdMutex();
    void giveSdMutex();
    void logf(int level, const char* tag, const char* fmt, ...) const;

    Config _cfg{};
    LogFn _log = nullptr;
    Audio _audio;
    SemaphoreHandle_t _sdMutex = nullptr;
    bool* _sdReady = nullptr;

    TaskHandle_t _task = nullptr;
    volatile bool _playing = false;
};
