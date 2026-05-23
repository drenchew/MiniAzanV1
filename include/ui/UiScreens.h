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
    static void lvCbBtn(lv_event_t* e);
    static void lvCbNav(lv_event_t* e);

private:
    void rebuildShell(UiScreenId id);
    void buildHome(lv_obj_t* area);
    void buildPrayerTimes(lv_obj_t* area);
    void buildAzanSettings(lv_obj_t* area);
    void buildFileManager(lv_obj_t* area);
    void buildQuranPlayer(lv_obj_t* area);
    void buildSystem(lv_obj_t* area);
    void requestDataForScreen(UiScreenId id);
    void sendCmd(UiCommand cmd, bool urgent = false);
    void populateFileList(const UiEventPayload& ev);
    void formatCountdown(char* buf, size_t len, int seconds);

    UiBridge* _bridge = nullptr;
    UiScreenMachine* _nav = nullptr;
    lv_obj_t* _root = nullptr;
    lv_obj_t* _contentArea = nullptr;
    UiStatusBarWidgets _statusBar{};
    UiBottomNavWidgets _bottomNav{};

    lv_obj_t* _lblClock = nullptr;
    lv_obj_t* _lblDate = nullptr;
    lv_obj_t* _lblPrayerNow = nullptr;
    lv_obj_t* _lblNextPrayer = nullptr;
    lv_obj_t* _barProgress = nullptr;
    lv_obj_t* _sliderVol = nullptr;
    lv_obj_t* _swPreFajr = nullptr;
    lv_obj_t* _listFiles = nullptr;
    lv_obj_t* _swWifi = nullptr;
    lv_obj_t* _lblSystem = nullptr;
    lv_obj_t* _scroll = nullptr;

    UiScreenId _screen = UiScreenId::Home;
    char _listFolder[64] = "/azan";
    char _pendingDelete[64]{};
    UiEventPayload _lastPrayer{};
    UiEventPayload _lastClock{};
    uint32_t _lastRefreshMs = 0;

    char _filePaths[16][72]{};
    uint8_t _filePathCount = 0;
    lv_obj_t* _prayerTimeLabels[5]{};
    lv_obj_t* _prayerCards[5]{};
};

#endif
