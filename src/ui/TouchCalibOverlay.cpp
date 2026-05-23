#include "ui/TouchCalibOverlay.h"

#if defined(MINI_AZAN_TOUCH_CALIB_MODE) && MINI_AZAN_TOUCH_CALIB_MODE

#include "SpiArchitecture.h"
#include "AppLog.h"

static const char* kCornerLabel[] = {"TOP-LEFT", "TOP-RIGHT", "BOTTOM-RIGHT", "BOTTOM-LEFT"};

bool TouchCalibOverlay::begin() {
    appLog(APP_LOG_INFO, "TCAL", "touch calib overlay — LVGL disabled");

    SpiArch::BusInitResult bus = SpiArch::initUiBus();
    if (!bus.ok) {
        appLog(APP_LOG_ERROR, "TCAL", "HSPI init failed");
        return false;
    }

    _tft.init();
    _tft.setRotation(0);
    _tft.fillScreen(TFT_BLACK);

    Xpt2046Touch::Config tcfg;
    tcfg.csPin = SpiArch::UI_TOUCH_CS;
    tcfg.tftCsPin = SpiArch::UI_TFT_CS;
    tcfg.sck = SpiArch::UI_SCK;
    tcfg.miso = SpiArch::UI_MISO;
    tcfg.mosi = SpiArch::UI_MOSI;
    tcfg.irqPin = SpiArch::UI_TOUCH_IRQ;
    tcfg.screenW = kScreenW;
    tcfg.screenH = kScreenH;
    tcfg.spiAvgSamples = 4;
    tcfg.deadzonePx = 3;
    tcfg.emaAlpha = 72;
    tcfg.pressDebounce = 3;
    tcfg.releaseDebounce = 3;

    if (!_touch.begin(tcfg, SpiArch::uiSpi())) {
        appLog(APP_LOG_ERROR, "TCAL", "touch begin failed");
        return false;
    }

    _touch.setRotation(Xpt2046Touch::Rotation::R0);

    Xpt2046Touch::Calibration bootCal;
    bootCal.rawXMin = 554;
    bootCal.rawXMax = 3672;
    bootCal.rawYMin = 320;
    bootCal.rawYMax = 3744;
    bootCal.invertY = true;
    bootCal.valid = true;
    _touch.setCalibration(bootCal);

    _phase = Phase::Calibrate;
    _calStep = 0;
    _waitRelease = false;
    drawFrame();
    drawCalStep();

    appLog(APP_LOG_INFO, "TCAL", "calibration 1/4 — touch TOP-LEFT, hold, lift");
    return true;
}

void TouchCalibOverlay::drawFrame() {
    _tft.fillScreen(TFT_NAVY);
    _tft.setTextColor(TFT_WHITE, TFT_NAVY);
    _tft.setTextSize(1);
    _tft.setCursor(4, 4);
    _tft.println("TOUCH CALIB (LVGL off)");
    _tft.drawFastHLine(0, 18, kScreenW, TFT_WHITE);
}

void TouchCalibOverlay::drawTarget(int step) {
    int16_t tx = 20, ty = 40;
    switch (step) {
        case 0: tx = 20; ty = 40; break;
        case 1: tx = kScreenW - 20; ty = 40; break;
        case 2: tx = kScreenW - 20; ty = kScreenH - 40; break;
        case 3: tx = 20; ty = kScreenH - 40; break;
        default: break;
    }
    _tft.fillCircle(tx, ty, 10, TFT_RED);
    _tft.drawCircle(tx, ty, 14, TFT_YELLOW);
}

void TouchCalibOverlay::drawCalStep() {
    _tft.fillRect(0, 20, kScreenW, kScreenH - 20, TFT_BLACK);
    _tft.setTextColor(TFT_WHITE, TFT_BLACK);
    _tft.setCursor(8, 28);
    _tft.printf("Step %d/4: %s", _calStep + 1, kCornerLabel[_calStep]);
    _tft.setCursor(8, 44);
    _tft.println("Hold on cross, then lift");
    drawTarget(_calStep);
}

void TouchCalibOverlay::drawTouchDot(int16_t x, int16_t y, uint16_t color) {
    if (_lastDotX >= 0) {
        _tft.fillCircle(_lastDotX, _lastDotY, 5, TFT_BLACK);
    }
    _tft.fillCircle(x, y, 5, color);
    _lastDotX = x;
    _lastDotY = y;
}

void TouchCalibOverlay::drawLiveHud(const Xpt2046Touch::TouchPoint& pt, bool down) {
    if (millis() - _lastDrawMs < 80) return;
    _lastDrawMs = millis();

    _tft.fillRect(0, 20, kScreenW, 72, TFT_BLACK);
    _tft.setTextColor(TFT_GREEN, TFT_BLACK);
    _tft.setCursor(4, 24);
    _tft.println("LIVE (calibrated)");
    _tft.setTextColor(TFT_WHITE, TFT_BLACK);
    _tft.setCursor(4, 38);
    _tft.printf("IRQ:%s raw %d,%d z", down ? "LOW" : "HIGH", pt.rawX, pt.rawY);
    _tft.setCursor(4, 50);
    _tft.printf("map %d,%d  out %d,%d", pt.mappedX, pt.mappedY, pt.x, pt.y);
    _tft.setCursor(4, 62);
    const auto& c = _touch.calibration();
    _tft.printf("X[%ld..%ld] Y[%ld..%ld]", (long)c.rawXMin, (long)c.rawXMax,
                (long)c.rawYMin, (long)c.rawYMax);

    if (down) {
        drawTouchDot(pt.x, pt.y, TFT_CYAN);
    } else if (_lastDotX >= 0) {
        _tft.fillCircle(_lastDotX, _lastDotY, 5, TFT_BLACK);
        _lastDotX = _lastDotY = -1;
    }
}

void TouchCalibOverlay::logSample(const char* kind, const Xpt2046Touch::TouchPoint& pt, int stage) {
    appLogf(APP_LOG_INFO, "TCAL",
            "%s stage=%d irq=%s raw_x=%d raw_y=%d z=%u map_x=%d map_y=%d out_x=%d out_y=%d invY=%d",
            kind, stage,
            _touch.irqLow() ? "LOW" : "HIGH",
            pt.rawX, pt.rawY, (unsigned)pt.z,
            pt.mappedX, pt.mappedY, pt.x, pt.y,
            _touch.calibration().invertY ? 1 : 0);
}

void TouchCalibOverlay::tryCaptureCorner() {
    if (_waitRelease) {
        if (!_touch.irqLow()) {
            _waitRelease = false;
            _stableSinceMs = 0;
        }
        return;
    }

    if (!_touch.irqLow()) {
        _stableSinceMs = 0;
        return;
    }

    Xpt2046Touch::TouchPoint pt{};
    if (!_touch.readMappedPoint(pt)) return;

    if (_stableSinceMs == 0) {
        _stableSinceMs = millis();
        _stableRawX = pt.rawX;
        _stableRawY = pt.rawY;
        return;
    }

    const int dx = abs(pt.rawX - _stableRawX);
    const int dy = abs(pt.rawY - _stableRawY);
    if (dx > 40 || dy > 40) {
        _stableSinceMs = millis();
        _stableRawX = pt.rawX;
        _stableRawY = pt.rawY;
        return;
    }

    if (millis() - _stableSinceMs < 350) return;

    uint16_t rx = 0, ry = 0;
    if (!_touch.captureCalibrationCorner(_calStep, rx, ry)) return;

    _cornerRawX[_calStep] = rx;
    _cornerRawY[_calStep] = ry;
    logSample("corner_ok", pt, _calStep + 1);

    _calStep++;
    _waitRelease = true;
    _stableSinceMs = 0;

    if (_calStep >= 4) {
        finishCalibration();
        return;
    }

    drawCalStep();
    appLogf(APP_LOG_INFO, "TCAL", "calibration %d/4 — touch %s", _calStep + 1, kCornerLabel[_calStep]);
}

void TouchCalibOverlay::finishCalibration() {
    _touch.commitCalibrationFromCorners(_cornerRawX, _cornerRawY);

    const auto& c = _touch.calibration();
    appLogf(APP_LOG_INFO, "TCAL", "invertY=%d (high raw Y = top of screen)", c.invertY ? 1 : 0);

    _phase = Phase::Live;
    _lastDotX = _lastDotY = -1;
    _tft.fillScreen(TFT_BLACK);
    drawFrame();
    drawLiveHud({}, false);

    appLog(APP_LOG_INFO, "TCAL", "calibration done — live validation mode");
    appLogf(APP_LOG_INFO, "TCAL", "raw_x=[%ld,%ld] raw_y=[%ld,%ld]",
            (long)c.rawXMin, (long)c.rawXMax, (long)c.rawYMin, (long)c.rawYMax);
}

void TouchCalibOverlay::pollSerial() {
    if (!Serial.available()) return;
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line == "recal") {
        _phase = Phase::Calibrate;
        _calStep = 0;
        _waitRelease = false;
        drawFrame();
        drawCalStep();
        appLog(APP_LOG_INFO, "TCAL", "restart calibration");
    } else if (line == "usecal") {
        Xpt2046Touch::Calibration c;
        c.rawXMin = 554;
        c.rawXMax = 3672;
        c.rawYMin = 320;
        c.rawYMax = 3744;
        c.invertY = true;
        c.valid = true;
        _touch.setCalibration(c);
        _touch.resetFilter();
        _phase = Phase::Live;
        _tft.fillScreen(TFT_BLACK);
        drawFrame();
        drawLiveHud({}, false);
        appLog(APP_LOG_INFO, "TCAL", "applied captured cal — live mode");
    }
}

void TouchCalibOverlay::update() {
    pollSerial();

    if (_phase == Phase::Calibrate) {
        tryCaptureCorner();

        if (millis() - _lastSerialMs >= 150) {
            _lastSerialMs = millis();
            const bool irq = _touch.irqLow();
            if (irq) {
                Xpt2046Touch::TouchPoint pt{};
                if (_touch.readMappedPoint(pt)) {
                    logSample("cal", pt, _calStep + 1);
                }
            } else {
                appLogf(APP_LOG_INFO, "TCAL",
                        "cal stage=%d irq=HIGH raw=--- map=---",
                        _calStep + 1);
            }
        }
        return;
    }

    Xpt2046Touch::TouchPoint pt{};
    const bool down = _touch.getPointEx(pt);

    if (down && !_wasDown) {
        logSample("press", pt, 0);
    } else if (!down && _wasDown) {
        appLog(APP_LOG_INFO, "TCAL", "release irq=HIGH");
    }

    if (millis() - _lastSerialMs >= 100) {
        _lastSerialMs = millis();
        if (down) {
            logSample("live", pt, 0);
        } else {
            appLogf(APP_LOG_INFO, "TCAL",
                    "live irq=HIGH raw=--- map=--- out=---");
        }
    }

    drawLiveHud(pt, down);
    _wasDown = down;
}

#endif
