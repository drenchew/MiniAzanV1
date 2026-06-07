#pragma once

#include "ui/UiBridge.h"
#include "ui/UiTypes.h"
#include "AudioManager.h"
#include "StorageManager.h"
#include "StorageJobQueue.h"
#include "TimeManager.h"
#include "AppTypes.h"

class BluetoothManager;

struct AppServices {
    AudioManager* audio = nullptr;
    StorageManager* storage = nullptr;
    TimeManager* time = nullptr;
    StorageJobQueue* storageJobs = nullptr;
    BluetoothManager* bluetooth = nullptr;

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

    void (*saveVolumeToNvs)(uint8_t) = nullptr;
    void (*savePreFajrToNvs)(bool) = nullptr;
    void (*saveAzanIndexToNvs)(uint8_t) = nullptr;
    void (*saveAzanPathToNvs)(const char* path) = nullptr;
    bool (*readDayRecord)(int day, DayRecord& out) = nullptr;
};

class AppCoordinator {
public:
    static constexpr uint8_t kMaxDeferredCmds = 16;

    bool begin(UiBridge& bridge, const AppServices& svc);

    /** Drain UiBridge normal queue into internal buffer (SysCoord task only). */
    void ingestCommands();

    /** Pop one deferred command matching priority band, or false. */
    bool popDeferredAtPriority(uint8_t priorityBand, UiCommand& out);

    /** P0 fast lane — immediate stop, no audio queue wait. */
    void executeEmergencyStop(bool* isAudioPlaying);

    void dispatchCommand(const UiCommand& cmd);
    void pollStorageResults();
    bool tickStorageWorker();

    static uint8_t commandPriority(UiCmd cmd);

    UiBridge& bridge() { return *_bridge; }

private:
    static void storageEmitThunk(const UiEventPayload& ev, void* user);
    void emit(const UiEventPayload& ev);
    void handleStopAudio();
    void handleSetVolumePct(uint8_t pct);
    void handlePreFajr(bool on);
    void handleAzanIndex(uint8_t idx);
    void handleToggleTransferMode();
    void handleRequestBluetoothStatus();
    void handleCancelBluetoothTransfer();
    void handleRequestSystemStatus();
    void handleRequestClock();
    void handleRequestPrayer();
    void handleListAudioFiles();
    void handleListFolder(const char* path, uint8_t page);
    void handleDeleteFile(const char* path);
    void handleSelectAzanFile(const char* path);
    void handlePlayFile(const char* path);
    void handlePauseAudio();
    void handleResumeAudio();
    bool storageBlocked() const;
    static const char* prayerName(int idx);

    UiBridge* _bridge = nullptr;
    AppServices _svc{};
    UiCommand _deferred[kMaxDeferredCmds]{};
    uint8_t _deferredCount = 0;
};
