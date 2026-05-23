#pragma once

#include "ui/UiBridge.h"
#include "ui/UiTypes.h"
#include "AudioManager.h"
#include "StorageManager.h"
#include "TimeManager.h"
#include "AppTypes.h"


/**
 * Executes UiBridge commands on a non-LVGL context (main loop or dedicated task).
 * Only module allowed to combine UI requests with StorageManager / AudioManager / NVS / WiFi.
 */
struct AppServices {
    AudioManager* audio = nullptr;
    StorageManager* storage = nullptr;
    TimeManager* time = nullptr;

    bool* wifiIsOn = nullptr;
    bool* isAudioPlaying = nullptr;
    int* timeOffsetMinutes = nullptr;

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
    void (*saveVolumeToNvs)(uint8_t) = nullptr;
    void (*savePreFajrToNvs)(bool) = nullptr;
    void (*saveAzanIndexToNvs)(uint8_t) = nullptr;
    void (*saveAzanPathToNvs)(const char* path) = nullptr;
    bool (*readDayRecord)(int day, DayRecord& out) = nullptr;
};

class AppCoordinator {
public:
    bool begin(UiBridge& bridge, const AppServices& svc);
    void poll();

    UiBridge& bridge() { return *_bridge; }

private:
    void dispatch(const UiCommand& cmd);
    void emit(const UiEventPayload& ev);
    void handleStopAudio();
    void handleSetVolume(uint8_t v);
    void handlePreFajr(bool on);
    void handleAzanIndex(uint8_t idx);
    void handleToggleWifi();
    void handleRequestWifiStatus();
    void handleRequestClock();
    void handleRequestPrayer();
    void handleListAudioFiles();
    void handleDeleteFile(const char* name);
    void handleSelectAzanFile(const char* path);
    void handleListFiles(bool audioOnly);
    bool storageBlocked() const;

    UiBridge* _bridge = nullptr;
    AppServices _svc{};
    uint32_t _listSliceMs = 0;
};
