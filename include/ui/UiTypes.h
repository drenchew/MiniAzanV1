#pragma once

#include <Arduino.h>

/** UI → system (posted from LVGL task only). */
enum class UiCmd : uint8_t {
    None = 0,
    StopAudio,           // highest priority — process first
    Navigate,
    SetVolume,
    SetPreFajr,
    SetAzanIndex,
    ToggleWifi,
    RequestWifiStatus,
    RequestClock,
    RequestPrayerTimes,
    ListAudioFiles,
    DeleteFile,
    RefreshFileList,
    SelectAzanFile,
};

/** System → UI (consumed on LVGL task). */
enum class UiEvent : uint8_t {
    None = 0,
    CmdResult,
    AudioState,
    ClockUpdate,
    PrayerTimesUpdate,
    WifiStatus,
    VolumeState,
    PreFajrState,
    AzanIndexState,
    FileListReady,
    FileOpResult,
    StorageBusy,
};

enum class UiScreenId : uint8_t {
    Home = 0,
    Wifi,
    Volume,
    AzanSelect,
    PreFajr,
    Files,
    Settings,
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
    uint8_t volume;
};

struct UiCmdSetPreFajr {
    bool enabled;
};

struct UiCmdSetAzanIndex {
    uint8_t index;
};

struct UiCmdDeleteFile {
    char name[48];
};

struct UiCmdSelectAzanFile {
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
    bool preFajr = false;
    uint8_t azanIndex = 0;
    bool audioPlaying = false;
    bool wifiOn = false;
    int wifiRssi = -120;
    char ip[16]{};

    int hour = 0;
    int minute = 0;
    int second = 0;
    char clockSource[8]{};

    uint16_t prayerMinutes[6]{};
    int nextPrayerMinutes = -1;

    uint8_t fileCount = 0;
    UiFileEntry files[24];

    char message[64]{};
};

struct UiCommand {
    UiCmd cmd = UiCmd::None;
    UiCmdNavigate nav{};
    UiCmdSetVolume vol{};
    UiCmdSetPreFajr pf{};
    UiCmdSetAzanIndex az{};
    UiCmdDeleteFile del{};
    UiCmdSelectAzanFile azanPath{};
};
