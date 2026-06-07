#pragma once

#include <Arduino.h>

#if defined(MINI_AZAN_TOUCH_CALIB_MODE) && MINI_AZAN_TOUCH_CALIB_MODE

#include <TFT_eSPI.h>
#include "ui/Xpt2046Touch.h"

/**
 * Replaces LVGL/UIManager while MINI_AZAN_TOUCH_CALIB_MODE=1.
 * TFT_eSPI + Xpt2046 on HSPI only; does not touch SD/audio/RTC.
 */
class TouchCalibOverlay {
public:
    bool begin();
    void update();

    const Xpt2046Touch::Calibration& calibration() const { return _touch.calibration(); }

private:
    enum class Phase : uint8_t { Calibrate = 0, Live = 1 };

    void drawFrame();
    void drawCalStep();
    void drawLiveHud(const Xpt2046Touch::TouchPoint& pt, bool down);
    void drawTarget(int step);
    void drawTouchDot(int16_t x, int16_t y, uint16_t color);
    void tryCaptureCorner();
    void finishCalibration();
    void logSample(const char* kind, const Xpt2046Touch::TouchPoint& pt, int stage);
    void pollSerial();

    TFT_eSPI _tft;
    Xpt2046Touch _touch;

    Phase _phase = Phase::Calibrate;
    int _calStep = 0;
    bool _waitRelease = false;
    uint32_t _stableSinceMs = 0;
    int16_t _stableRawX = 0;
    int16_t _stableRawY = 0;
    uint16_t _cornerRawX[4]{};
    uint16_t _cornerRawY[4]{};

    uint32_t _lastDrawMs = 0;
    uint32_t _lastSerialMs = 0;
    bool _wasDown = false;
    int16_t _lastDotX = -1;
    int16_t _lastDotY = -1;

    static constexpr int kScreenW = 240;
    static constexpr int kScreenH = 320;
};

#endif
