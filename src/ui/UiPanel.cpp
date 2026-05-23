#include "ui/UiPanel.h"

#if defined(MINI_AZAN_UI_ENABLE) && MINI_AZAN_UI_ENABLE

static UiPanel s_panel;

UiPanel& UiPanel::instance() {
    return s_panel;
}

bool UiPanel::begin(int width, int height) {
    _tft.init();
    _tft.setRotation(0);
    _tft.fillScreen(TFT_BLACK);

    Xpt2046Touch::Config tcfg;
    tcfg.csPin = SpiArch::UI_TOUCH_CS;
    tcfg.sck = SpiArch::UI_SCK;
    tcfg.miso = SpiArch::UI_MISO;
    tcfg.mosi = SpiArch::UI_MOSI;
    tcfg.screenW = width;
    tcfg.screenH = height;
    _touch.begin(tcfg, SpiArch::uiSpi());

    lv_init();

    const size_t bufPixels = (size_t)width * kBufLines;
    _buf1 = (lv_color_t*)heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!_buf1) return false;

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

    return true;
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
    int16_t x = 0, y = 0;
    if (instance()._touch.getPoint(x, y)) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

#endif
