#pragma once

#include "ui/UiBridge.h"
#include "ui/UiTypes.h"
#include "AudioManager.h"
#include "StorageManager.h"
#include "StorageJobQueue.h"
#include "TimeManager.h"
#include "AppTypes.h"

struct AppServices {
    AudioManager* audio = nullptr;
    StorageManager* storage = nullptr;
    TimeManager* time = nullptr;
    StorageJobQueue* storageJobs = nullptr;

    bool* wifiIsOn = nullptr;
    bool* isAudioPlaying = nullptr;

    bool* preFajrEnabled = nullptr;
    uint8_t* currentVolume = nullptr;
    uint8_t minVolume = 0;
    uint8_t maxVolume = 21;
    int* currentAzanIndex = nullptr;
    int numAzanFiles = 0;
    const char* const* azanFiles = nullptr;
    char* uiSelectedAzanPath = nullptr;
    size_t uiSelectedAzanPathSize = 0;

    const char* prayerBinPath = nullptr;
    size_t prayerRecordSize = 0;
    DayRecord* cachedPrayerTimes = nullptr;
    int* cachedPrayerDay = nullptr;
    bool* cachedPrayerTimesValid = nullptr;

    void (*toggleWifi)() = nullptr;
    void (*playFile)(const char* path) = nullptr;
    void (*saveVolumeToNvs)(uint8_t) = nullptr;
    void (*savePreFajrToNvs)(bool) = nullptr;
    void (*saveAzanIndexToNvs)(uint8_t) = nullptr;
    void (*saveAzanPathToNvs)(const char* path) = nullptr;
    void (*saveWifiLastToNvs)(bool on) = nullptr;
    bool (*readDayRecord)(int day, DayRecord& out) = nullptr;
};

class AppCoordinator {
public:
    bool begin(UiBridge& bridge, const AppServices& svc);
    void poll();

    UiBridge& bridge() { return *_bridge; }

private:
    static void storageEmitThunk(const UiEventPayload& ev, void* user);
    void dispatch(const UiCommand& cmd);
    void emit(const UiEventPayload& ev);
    void handleStopAudio();
    void handleSetVolumePct(uint8_t pct);
    void handlePreFajr(bool on);
    void handleAzanIndex(uint8_t idx);
    void handleToggleWifi();
    void handleRequestWifiStatus();
    void handleRequestSystemStatus();
    void handleRequestClock();
    void handleRequestPrayer();
    void handleListAudioFiles();
    void handleListFolder(const char* path, uint8_t page);
    void handleDeleteFile(const char* path);
    void handleSelectAzanFile(const char* path);
    void handlePlayFile(const char* path);
    void handleListFiles(bool audioOnly);
    bool storageBlocked() const;
    static const char* prayerName(int idx);

    UiBridge* _bridge = nullptr;
    AppServices _svc{};
};
