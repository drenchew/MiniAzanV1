#pragma once

#include "ui/UiTypes.h"
#include "ui/UiBridge.h"
#include "ui/UiScreenMachine.h"
#include "ui/UiComponents.h"

#if defined(MINI_AZAN_UI_ENABLE) && MINI_AZAN_UI_ENABLE

#include <lvgl.h>

class UiScreens {
public:
    void begin(UiBridge& bridge, UiScreenMachine& nav);
    void show(UiScreenId id);
    void onEvent(const UiEventPayload& ev);
    void tickRefresh();
    void sendCmd(UiCommand cmd, bool urgent = false);
    void setPrayerArcIcons(const char* const icons[6]);

    static void lvCbBtn(lv_event_t* e);
    static void lvCbNav(lv_event_t* e);

private:
    struct ArcPoint {
        lv_coord_t x = 0;
        lv_coord_t y = 0;
    };

    // ── Screen builders ───────────────────────────────────────────────────
    void rebuildShell(UiScreenId id);
    void buildHome(lv_obj_t* area);
    void buildMenu(lv_obj_t* area);
    void buildPrayerTimes(lv_obj_t* area);
    void buildAzanSettings(lv_obj_t* area);
    void buildFileManager(lv_obj_t* area);
    void buildQuranPlayer(lv_obj_t* area);
    void buildSystem(lv_obj_t* area);

    // Returns the 34-px sub-screen header; content starts at y=kSubHdrH
    lv_obj_t* buildSubScreenHeader(lv_obj_t* area, const char* title);

    void requestDataForScreen(UiScreenId id);
    void requestPrayerTimesForDay(int yday);
    void requestFolderList(const char* path);
    void requestFolderPage(const char* path, uint8_t page);
    bool isCurrentListResult(const UiEventPayload& ev) const;
    void populateFileList(const UiEventPayload& ev);
    void updateQuranPager();
    void formatCountdown(char* buf, size_t len, int seconds);

    // ── Sun-path arc helpers ──────────────────────────────────────────────
    void updateSunArc();
    bool prayerProgressFromMinutes(int prayerMinutes, float& progress) const;
    bool currentSunProgress(float& progress) const;
    ArcPoint arcPointForProgress(float progress) const;
    void setArcObjectCenter(lv_obj_t* obj, const ArcPoint& p);
    void stylePrayerArcMarker(int idx, bool active);
    void updatePrayerArcMarkers();

    // ── Root LVGL tree ────────────────────────────────────────────────────
    UiBridge*         _bridge      = nullptr;
    UiScreenMachine*  _nav         = nullptr;
    lv_obj_t*         _root        = nullptr;
    lv_obj_t*         _contentArea = nullptr;
    UiStatusBarWidgets _statusBar{};
    // No bottom nav in the new design

    // ── Home screen ───────────────────────────────────────────────────────
    lv_obj_t* _lblClock      = nullptr;  // big live time
    lv_obj_t* _lblDate       = nullptr;  // date below clock
    lv_obj_t* _lblPrayerNow  = nullptr;  // "Now: Dhuhr"
    lv_obj_t* _lblNextPrayer = nullptr;  // "Next: Asr in 01:24:10"
    lv_obj_t* _arcSunPath    = nullptr;  // sun-path arc widget
    lv_obj_t* _sunMarker     = nullptr;  // moving sun dot using exact arc math
    lv_obj_t* _prayerArcMarkers[6]{};
    lv_obj_t* _prayerArcMarkerIcons[6]{};
    const char* _prayerArcIcons[6]{};
    lv_obj_t* _lblArcFajr    = nullptr;  // Fajr time label near arc left endpoint
    lv_obj_t* _lblArcIsha    = nullptr;  // Isha time label near arc right endpoint
    lv_obj_t* _btnStopAzan   = nullptr;  // visible only while azan is actively calling

    // ── Azan Settings ─────────────────────────────────────────────────────
    lv_obj_t* _sliderVol  = nullptr;
    lv_obj_t* _swPreFajr  = nullptr;

    // ── File Manager / Quran Player ───────────────────────────────────────
    lv_obj_t* _listFiles      = nullptr;
    lv_obj_t* _lblNowPlaying  = nullptr;
    lv_obj_t* _btnPauseResume = nullptr;
    lv_obj_t* _lblCurrentPath = nullptr;
    lv_obj_t* _lblPageInfo    = nullptr;
    lv_obj_t* _btnUpFolder    = nullptr;
    lv_obj_t* _btnPrevPage    = nullptr;
    lv_obj_t* _btnNextPage    = nullptr;

    // ── Bluetooth screen ──────────────────────────────────────────────────
    lv_obj_t* _swTransfer    = nullptr;
    lv_obj_t* _barBtProgress = nullptr;
    lv_obj_t* _lblSystem     = nullptr;

    // ── Misc shared ───────────────────────────────────────────────────────
    lv_obj_t* _barProgress = nullptr;  // kept for compatibility (unused on home now)
    lv_obj_t* _scroll      = nullptr;
    lv_obj_t* _lblPrayerDate = nullptr;
    lv_obj_t* _prayerTimeLabels[6]{};
    lv_obj_t* _prayerCards[6]{};

    // ── Navigation / state ────────────────────────────────────────────────
    UiScreenId _screen = UiScreenId::Home;

    // File browsing
    char _listFolder[UI_PATH_MAX] = "/azan";
    char _pendingDelete[UI_PATH_MAX]{};
    char _currentPlayingPath[UI_PATH_MAX]{};
    char _currentBrowsePath[UI_PATH_MAX] = "/";

    // Async list request tracking
    char     _activeListFolder[UI_PATH_MAX] = "/";
    uint32_t _listRequestSeq       = 0;
    uint32_t _activeListRequestId  = 0;
    uint8_t  _activeListPage       = 0;
    uint8_t  _activeListTotal      = 0;

    // Audio state
    bool _isAudioPlaying = false;
    bool _isAudioPaused = false;
    bool _isAzanPlaying = false;

    // Last known events (for arc updates)
    UiEventPayload _lastPrayer{};
    UiEventPayload _lastClock{};
    int _selectedPrayerYday = 0;

    uint32_t _lastRefreshMs = 0;

    // File path cache
    char    _filePaths[UI_FILE_PAGE_SIZE][UI_PATH_MAX]{};
    uint8_t _filePathCount = 0;
    bool    _isFolder[UI_FILE_PAGE_SIZE]{};

    // Streaming list batch buffer
    UiFileEntry _streamFiles[UI_FILE_PAGE_SIZE]{};
    uint8_t     _streamCount = 0;
};

#endif
