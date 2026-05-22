#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include "freertos/semphr.h"
#include "SpiArchitecture.h"

/**
 * Sole owner of VSPI + SD card. All non-audio file I/O goes through here.
 * AudioManager may call mediaFs() + playback lock APIs only.
 * UIManager must NEVER include this header for SD access.
 */
class StorageManager {
public:
    struct Config {
        int csPin = SpiArch::SD_CS;
        int sck = SpiArch::SD_SCK;
        int miso = SpiArch::SD_MISO;
        int mosi = SpiArch::SD_MOSI;
        TickType_t mutexTimeout = pdMS_TO_TICKS(200);
    };

    using LogFn = void (*)(int level, const char* tag, const char* message);

    bool begin(const Config& cfg, LogFn logFn = nullptr);
    bool begin(LogFn logFn = nullptr) { return begin(Config{}, logFn); }
    bool isReady() const { return _ready; }
    bool isPlaybackLocked() const { return _playbackLocked; }

    /** Called only by AudioManager when azan decode is active on VSPI. */
    void setPlaybackLocked(bool locked);

    /**
     * SD filesystem for ESP32-audioI2S connecttoFS only.
     * Do not open files from main/web/UI — use the APIs below.
     */
    FS& mediaFs();

    bool fileExists(const char* path);
    uint32_t fileSize(const char* path);
    bool removeFile(const char* path);
    bool readRecordAt(const char* binPath, int recordIndex, void* out, size_t recordSize);

    bool uploadBegin(const char* filename);
    bool uploadWrite(const uint8_t* data, size_t len);
    void uploadEnd(bool success);

    bool listRootFilesJson(String& jsonOut);
    int listRootFilesDebug(void (*logLine)(const char* line));

private:
    bool takeLock(TickType_t timeout, bool ignorePlaybackLock = false);
    void giveLock();
    void logf(int level, const char* tag, const char* fmt, ...) const;
    static String normalizePath(const char* path);

    Config _cfg{};
    LogFn _log = nullptr;
    SemaphoreHandle_t _mutex = nullptr;
    bool _ready = false;
    volatile bool _playbackLocked = false;
    File _uploadFile;
};
