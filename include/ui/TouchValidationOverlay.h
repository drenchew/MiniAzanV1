#pragma once

#include <Arduino.h>

#if defined(MINI_AZAN_TOUCH_VALIDATION_MODE) && MINI_AZAN_TOUCH_VALIDATION_MODE

#include <TFT_eSPI.h>
#include "ui/Xpt2046Touch.h"

/** LVGL bypass: fixed-calibration touch trail test (audio/RTC/SD unchanged). */
class TouchValidationOverlay {
public:
    bool begin();
    void update();

    static constexpr int kScreenW = 240;
    static constexpr int kScreenH = 320;

    static constexpr int32_t kRawXMin = 554;
    static constexpr int32_t kRawXMax = 3672;
    static constexpr int32_t kRawYMin = 320;
    static constexpr int32_t kRawYMax = 3744;

private:
    void drawStaticUi();
    void drawCornerGuides();
    void plotTrail(int16_t x, int16_t y);
    void logActive(const Xpt2046Touch::TouchPoint& pt);

    static Xpt2046Touch::Calibration fixedCalibration();

    TFT_eSPI _tft;
    Xpt2046Touch _touch;

    uint32_t _lastSerialMs = 0;
    uint32_t _lastPlotMs = 0;
    bool _wasDown = false;
};

#endif
