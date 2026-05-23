#pragma once

#include <Arduino.h>

/** UI → system (posted from LVGL task only). */
enum class UiCmd : uint8_t {
    None = 0,
    StopAudio,
    Navigate,
    SetVolume,
    SetPreFajr,
    SetAzanIndex,
    ToggleWifi,
    RequestWifiStatus,
    RequestClock,
    RequestPrayerTimes,
    RequestSystemStatus,
    ListAudioFiles,
    ListFolder,
    DeleteFile,
    RefreshFileList,
    SelectAzanFile,
    PlayFile,
    PauseAudio,
    ResumeAudio,
};

/** System → UI (consumed on LVGL task). */
enum class UiEvent : uint8_t {
    None = 0,
    CmdResult,
    AudioState,
    ClockUpdate,
    PrayerTimesUpdate,
    WifiStatus,
    SystemStatus,
    VolumeState,
    PreFajrState,
    AzanIndexState,
    FileListReady,
    FileOpResult,
    StorageBusy,
};

enum class UiScreenId : uint8_t {
    Home = 0,
    PrayerTimes,
    AzanSettings,
    FileManager,
    QuranPlayer,
    System,
    /** @deprecated aliases for migration */
    Wifi = System,
    Volume = AzanSettings,
    AzanSelect = AzanSettings,
    PreFajr = AzanSettings,
    Files = FileManager,
    Settings = AzanSettings,
};

enum class UiResult : uint8_t {
    Ok = 0,
    Busy,
    Failed,
    Rejected,
};

struct UiCmdNavigate {
    UiScreenId screen;
    bool pushStack;
};

struct UiCmdSetVolume {
    uint8_t volumePct; /**< 0–100 UI scale */
};

struct UiCmdSetPreFajr {
    bool enabled;
};

struct UiCmdSetAzanIndex {
    uint8_t index;
};

struct UiCmdListFolder {
    char path[64];
    uint8_t page;
};

struct UiCmdDeleteFile {
    char path[64];
};

struct UiCmdSelectAzanFile {
    char path[64];
};

struct UiCmdPlayFile {
    char path[64];
};

struct UiFileEntry {
    char name[48];
    uint32_t size;
};

struct UiEventPayload {
    UiEvent type = UiEvent::None;
    UiResult result = UiResult::Ok;
    UiScreenId screen = UiScreenId::Home;

    uint8_t volume = 0;
    uint8_t volumePct = 0;
    bool preFajr = false;
    uint8_t azanIndex = 0;
    bool audioPlaying = false;
    bool wifiOn = false;
    int wifiRssi = -120;
    char ip[16]{};

    bool sdReady = false;
    bool rtcOk = false;
    bool rtcBatteryFail = false;
    char timeSource[12]{};

    int year = 2026;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int yday = 0;
    int month = 0;
    int mday = 0;
    char clockSource[8]{};

    uint16_t prayerMinutes[6]{};
    int currentPrayerIndex = -1;
    int nextPrayerIndex = -1;
    int nextPrayerMinutes = -1;
    int secondsToNext = -1;
    char currentPrayerName[16]{};
    char nextPrayerName[16]{};

    uint8_t fileCount = 0;
    UiFileEntry files[16];
    char listFolder[64]{};
    uint8_t listPage = 0;
    uint8_t listTotal = 0;
    uint32_t listRequestId = 0;
    char defaultAzanPath[64]{};

    char message[64]{};
};

struct UiCommand {
    UiCmd cmd = UiCmd::None;
    UiCmdNavigate nav{};
    UiCmdSetVolume vol{};
    UiCmdSetPreFajr pf{};
    UiCmdSetAzanIndex az{};
    UiCmdListFolder list{};
    UiCmdDeleteFile del{};
    UiCmdSelectAzanFile azanPath{};
    UiCmdPlayFile play{};
};
