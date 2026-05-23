#pragma once

#include "ui/UiTypes.h"
#include "ui/UiBridge.h"
#include "ui/UiScreenMachine.h"

#if defined(MINI_AZAN_UI_ENABLE) && MINI_AZAN_UI_ENABLE

#include <lvgl.h>

class UiScreens {
public:
    void begin(UiBridge& bridge, UiScreenMachine& nav);
    void show(UiScreenId id);
    void onEvent(const UiEventPayload& ev);
    void tickRefresh();
    static void lvCbBtn(lv_event_t* e);

private:
    void clearContent();
    void buildHome();
    void buildWifi();
    void buildVolume();
    void buildAzan();
    void buildPreFajr();
    void buildFiles();
    void postNav(UiScreenId id, bool push);
    void sendCmd(UiCommand cmd, bool urgent = false);

    UiBridge* _bridge = nullptr;
    UiScreenMachine* _nav = nullptr;
    lv_obj_t* _root = nullptr;
    lv_obj_t* _header = nullptr;
    lv_obj_t* _content = nullptr;
    lv_obj_t* _lblClock = nullptr;
    lv_obj_t* _lblPrayer = nullptr;
    lv_obj_t* _lblStatus = nullptr;
    lv_obj_t* _sliderVol = nullptr;
    lv_obj_t* _swPreFajr = nullptr;
    lv_obj_t* _listFiles = nullptr;
    lv_obj_t* _swWifi = nullptr;
    lv_timer_t* _refreshTimer = nullptr;
    uint32_t _lastRefreshMs = 0;
};

#endif
