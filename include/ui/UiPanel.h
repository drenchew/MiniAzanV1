#pragma once

#include "SpiArchitecture.h"
#include "ui/Xpt2046Touch.h"

#if defined(MINI_AZAN_UI_ENABLE) && MINI_AZAN_UI_ENABLE

#include <lvgl.h>
#include <TFT_eSPI.h>

class UiPanel {
public:
    bool begin(int width, int height);
    bool runTouchDiagnostics();
    TFT_eSPI& tft() { return _tft; }
    Xpt2046Touch& touch() { return _touch; }

    static UiPanel& instance();

private:
    static void flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color);
    static void touchCb(lv_indev_drv_t* drv, lv_indev_data_t* data);

    TFT_eSPI _tft;
    Xpt2046Touch _touch;
    lv_disp_draw_buf_t _drawBuf{};
    lv_disp_drv_t _dispDrv{};
    lv_indev_drv_t _indevDrv{};
    lv_disp_t* _disp = nullptr;
    lv_indev_t* _indev = nullptr;
    lv_color_t* _buf1 = nullptr;
    lv_color_t* _buf2 = nullptr;  ///< second DMA buffer for double-buffering
    int16_t _lastTouchX = 0;
    int16_t _lastTouchY = 0;
    // kBufLines MUST match MemCfg::LVGL_DRAW_LINES in MemoryManager.h
    static constexpr int kBufLines = 20;
};

#endif
