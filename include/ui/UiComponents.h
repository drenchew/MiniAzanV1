#pragma once

#include "ui/UiTypes.h"

#if defined(MINI_AZAN_UI_ENABLE) && MINI_AZAN_UI_ENABLE

#include <lvgl.h>

struct UiStatusBarWidgets {
    lv_obj_t* root = nullptr;
    lv_obj_t* bt = nullptr;
    lv_obj_t* timeSrc = nullptr;
    lv_obj_t* vol = nullptr;
    lv_obj_t* sd = nullptr;
    lv_obj_t* batt = nullptr;
    uint8_t lastVolumePct = 0;
    bool sdKnown = false;
    bool sdReady = false;
};

struct UiBottomNavWidgets {
    lv_obj_t* root = nullptr;
    lv_obj_t* btns[5]{};
};

class UiComponents {
public:
    static lv_obj_t* createCard(lv_obj_t* parent, lv_coord_t w, lv_coord_t h);
    static UiStatusBarWidgets createStatusBar(lv_obj_t* parent, lv_coord_t width);
    static void updateStatusBar(UiStatusBarWidgets& sb, const UiEventPayload& ev);
    static UiBottomNavWidgets createBottomNav(lv_obj_t* parent, UiScreenId active,
                                              void (*cb)(lv_event_t*));
    static void highlightNav(UiBottomNavWidgets& nav, UiScreenId active);
    static lv_obj_t* createScrollContent(lv_obj_t* parent, lv_coord_t y, lv_coord_t h);
    static void showConfirmDialog(lv_obj_t* parent, const char* title, const char* msg,
                                  void (*onYes)(lv_event_t*), void* userData);
    static void dismissDialog(lv_obj_t* dlg);
};

#endif
