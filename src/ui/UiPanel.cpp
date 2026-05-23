#include "ui/UiPanel.h"

#if defined(MINI_AZAN_UI_ENABLE) && MINI_AZAN_UI_ENABLE

#include "AppLog.h"

#ifndef MINI_AZAN_TOUCH_DEBUG
#define MINI_AZAN_TOUCH_DEBUG 0
#endif

static UiPanel s_panel;

UiPanel& UiPanel::instance() {
    return s_panel;
}

bool UiPanel::begin(int width, int height) {
    appLog(APP_LOG_INFO, "UI", "panel begin step=hspi_init");
    SpiArch::BusInitResult bus = SpiArch::initUiBus();
    if (!bus.ok) {
        appLogf(APP_LOG_ERROR, "UI",
                "HSPI init FAIL host=%d expect=%d pins %d,%d,%d",
                bus.hostId, SpiArch::UI_HOST, bus.sck, bus.miso, bus.mosi);
        return false;
    }
    appLogf(APP_LOG_INFO, "UI",
             "HSPI ok host=%d SCK=%d MISO=%d MOSI=%d (not VSPI)",
             bus.hostId, bus.sck, bus.miso, bus.mosi);

    appLog(APP_LOG_INFO, "UI", "panel begin step=tft_init (TFT_eSPI also uses HSPI)");
    _tft.init();
    _tft.setRotation(0);
    _tft.fillScreen(TFT_BLACK);
    SpiArch::releaseTftChipSelect();
    appLog(APP_LOG_INFO, "UI", "tft_init done tft_cs=released");

    Xpt2046Touch::Config tcfg;
    tcfg.csPin = SpiArch::UI_TOUCH_CS;
    tcfg.tftCsPin = SpiArch::UI_TFT_CS;
    tcfg.sck = SpiArch::UI_SCK;
    tcfg.miso = SpiArch::UI_MISO;
    tcfg.mosi = SpiArch::UI_MOSI;
    tcfg.irqPin = SpiArch::UI_TOUCH_IRQ;
    tcfg.screenW = width;
    tcfg.screenH = height;
    tcfg.zThreshold = 200;
    tcfg.pressDebounce = 3;
    tcfg.releaseDebounce = 3;
    tcfg.spiAvgSamples = 3;
    tcfg.deadzonePx = 4;
    tcfg.emaAlpha = 96;

    appLog(APP_LOG_INFO, "UI", "panel begin step=touch_init");
    if (!_touch.begin(tcfg, SpiArch::uiSpi())) {
        appLog(APP_LOG_ERROR, "UI", "touch_init FAIL");
        return false;
    }

    Xpt2046Touch::Calibration cal;
    cal.rawXMin = 300;
    cal.rawXMax = 3700;
    cal.rawYMin = 300;
    cal.rawYMax = 3700;
    cal.valid = true;
    _touch.setCalibration(cal);
    _touch.setRotation(Xpt2046Touch::Rotation::R0);

    appLog(APP_LOG_INFO, "UI", "touch_init ok (run TOUCHDBG / cal in touch_test sketch)");

    _touch.runDiagnostics("TCH");

    lv_init();

    const size_t bufPixels = (size_t)width * kBufLines;
    _buf1 = (lv_color_t*)heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!_buf1) {
        appLog(APP_LOG_ERROR, "UI", "lvgl draw buffer alloc failed");
        return false;
    }

    lv_disp_draw_buf_init(&_drawBuf, _buf1, nullptr, (uint32_t)bufPixels);

    lv_disp_drv_init(&_dispDrv);
    _dispDrv.hor_res = (lv_coord_t)width;
    _dispDrv.ver_res = (lv_coord_t)height;
    _dispDrv.flush_cb = flushCb;
    _dispDrv.draw_buf = &_drawBuf;
    lv_disp_drv_register(&_dispDrv);

    lv_indev_drv_init(&_indevDrv);
    _indevDrv.type = LV_INDEV_TYPE_POINTER;
    _indevDrv.read_cb = touchCb;
    lv_indev_drv_register(&_indevDrv);

    appLog(APP_LOG_INFO, "UI", "panel begin complete");
    return true;
}

bool UiPanel::runTouchDiagnostics() {
    return _touch.runDiagnostics("TCH");
}

void UiPanel::flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    TFT_eSPI& tft = instance()._tft;
    uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)color_p, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}

void UiPanel::touchCb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    (void)drv;
    UiPanel& panel = instance();
    int16_t x = 0, y = 0;
    const bool pressed = panel._touch.getPoint(x, y);

    if (pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;

#if MINI_AZAN_TOUCH_DEBUG
        static int16_t lastX = -1, lastY = -1;
        if (lastX != x || lastY != y) {
            TFT_eSPI& tft = panel._tft;
            tft.startWrite();
            if (lastX >= 0) {
                tft.fillCircle(lastX, lastY, 6, TFT_BLACK);
            }
            tft.fillCircle(x, y, 6, TFT_RED);
            tft.endWrite();
            lastX = x;
            lastY = y;
        }
#endif
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
#if MINI_AZAN_TOUCH_DEBUG
        static int16_t lastX = -1, lastY = -1;
        if (lastX >= 0) {
            panel._tft.fillCircle(lastX, lastY, 6, TFT_BLACK);
            lastX = -1;
        }
#endif
    }
}

#endif
