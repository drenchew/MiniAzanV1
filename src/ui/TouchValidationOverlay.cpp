#include "ui/TouchValidationOverlay.h"

#if defined(MINI_AZAN_TOUCH_VALIDATION_MODE) && MINI_AZAN_TOUCH_VALIDATION_MODE

#include "SpiArchitecture.h"
#include "AppLog.h"

Xpt2046Touch::Calibration TouchValidationOverlay::fixedCalibration() {
    Xpt2046Touch::Calibration c{};
    c.rawXMin = kRawXMin;
    c.rawXMax = kRawXMax;
    c.rawYMin = kRawYMin;
    c.rawYMax = kRawYMax;
    c.invertY = true;
    c.invertX = false;
    c.valid = true;
    return c;
}

bool TouchValidationOverlay::begin() {
    appLog(APP_LOG_INFO, "TVAL", "touch validation mode — LVGL disabled");

    if (!SpiArch::initUiBus().ok) {
        appLog(APP_LOG_ERROR, "TVAL", "HSPI init failed");
        return false;
    }

    _tft.init();
    _tft.setRotation(0);
    _tft.fillScreen(TFT_BLACK);

    Xpt2046Touch::Config cfg;
    cfg.csPin = SpiArch::UI_TOUCH_CS;
    cfg.tftCsPin = SpiArch::UI_TFT_CS;
    cfg.sck = SpiArch::UI_SCK;
    cfg.miso = SpiArch::UI_MISO;
    cfg.mosi = SpiArch::UI_MOSI;
    cfg.irqPin = SpiArch::UI_TOUCH_IRQ;
    cfg.screenW = kScreenW;
    cfg.screenH = kScreenH;
    cfg.spiAvgSamples = 4;
    cfg.deadzonePx = 2;
    cfg.emaAlpha = 128;
    cfg.pressDebounce = 2;
    cfg.releaseDebounce = 2;

    if (!_touch.begin(cfg, SpiArch::uiSpi())) {
        appLog(APP_LOG_ERROR, "TVAL", "touch init failed");
        return false;
    }

    _touch.setRotation(Xpt2046Touch::Rotation::R0);
    _touch.setCalibration(fixedCalibration());
    _touch.resetFilter();

    drawStaticUi();

    appLogf(APP_LOG_INFO, "TVAL",
            "cal x=[%ld,%ld] y=[%ld,%ld] invertY=1 map=0..%d,0..%d",
            (long)kRawXMin, (long)kRawXMax, (long)kRawYMin, (long)kRawYMax,
            kScreenW - 1, kScreenH - 1);
    appLog(APP_LOG_INFO, "TVAL", "touch corners: TL TR BR BL — trail=cyan");
    return true;
}

void TouchValidationOverlay::drawStaticUi() {
    drawCornerGuides();
    _tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    _tft.setTextSize(1);
    _tft.setCursor(4, 4);
    _tft.print("TOUCH VALIDATE");
    _tft.drawFastHLine(0, 14, kScreenW, TFT_DARKGREY);
}

void TouchValidationOverlay::drawCornerGuides() {
    const int m = 8;
    auto cross = [&](int16_t x, int16_t y) {
        _tft.drawFastHLine(x - m, y, m * 2 + 1, TFT_DARKGREY);
        _tft.drawFastVLine(x, y - m, m * 2 + 1, TFT_DARKGREY);
    };
    cross(0, 0);
    cross(kScreenW - 1, 0);
    cross(kScreenW - 1, kScreenH - 1);
    cross(0, kScreenH - 1);
}

void TouchValidationOverlay::plotTrail(int16_t x, int16_t y) {
    if (millis() - _lastPlotMs < 16) return;
    _lastPlotMs = millis();

    _tft.startWrite();
    _tft.fillCircle(x, y, 2, TFT_CYAN);
    _tft.fillCircle(x, y, 1, TFT_WHITE);
    _tft.endWrite();
}

void TouchValidationOverlay::logActive(const Xpt2046Touch::TouchPoint& pt) {
    if (millis() - _lastSerialMs < 80) return;
    _lastSerialMs = millis();

    appLogf(APP_LOG_INFO, "TVAL",
            "irq=LOW raw_x=%d raw_y=%d z=%u mapped_x=%d mapped_y=%d",
            pt.rawX, pt.rawY, (unsigned)pt.z, pt.mappedX, pt.mappedY);
}

void TouchValidationOverlay::update() {
    if (!_touch.irqLow()) {
        if (_wasDown) {
            appLog(APP_LOG_INFO, "TVAL", "irq=HIGH (released)");
        }
        _wasDown = false;
        return;
    }

    Xpt2046Touch::TouchPoint pt{};
    if (!_touch.readMappedPoint(pt)) {
        return;
    }

    if (!_wasDown) {
        appLog(APP_LOG_INFO, "TVAL", "irq=LOW (pressed)");
    }
    _wasDown = true;

    plotTrail(pt.mappedX, pt.mappedY);
    logActive(pt);
}

#endif
