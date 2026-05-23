#include "ui/Xpt2046Touch.h"
#include "SpiArchitecture.h"
#include <Arduino.h>

#if defined(MINI_AZAN_TOUCH_STANDALONE) && MINI_AZAN_TOUCH_STANDALONE
#define appLog(level, tag, msg) Serial.println(msg)
#define appLogf(level, tag, fmt, ...) Serial.printf(fmt "\n", ##__VA_ARGS__)
#else
#include "AppLog.h"
#endif

namespace {

constexpr uint16_t kSaturation = 4080;

bool irqGated(const Xpt2046Touch::Config& cfg) {
    return cfg.irqPin >= 0;
}

int16_t clamp16(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return (int16_t)lo;
    if (v > hi) return (int16_t)hi;
    return (int16_t)v;
}

}  // namespace

bool Xpt2046Touch::irqLow() const {
    if (_cfg.irqPin < 0) return false;
    return digitalRead(_cfg.irqPin) == LOW;
}

void Xpt2046Touch::prepareBusForTouch() const {
    SpiArch::releaseTftChipSelect();
    digitalWrite(_cfg.csPin, HIGH);
}

void Xpt2046Touch::setCalibration(const Calibration& cal) {
    _cal = cal;
}

void Xpt2046Touch::resetFilter() {
    _emaInit = false;
    _emaX = 0;
    _emaY = 0;
    _stableX = 0;
    _stableY = 0;
}

void Xpt2046Touch::onRelease() {
    resetFilter();
    _pressEventSent = false;
}

void Xpt2046Touch::updateIrqDebounce() {
    if (!irqGated(_cfg)) return;

    if (irqLow()) {
        _irqLowStreak++;
        _irqHighStreak = 0;
        if (!_pressed && _irqLowStreak >= _cfg.pressDebounce) {
            _pressed = true;
        }
    } else {
        _irqHighStreak++;
        _irqLowStreak = 0;
        if (_pressed && _irqHighStreak >= _cfg.releaseDebounce) {
            _pressed = false;
            onRelease();
        }
    }
}

bool Xpt2046Touch::begin(const Config& cfg, SPIClass& spi) {
    _cfg = cfg;
    _spi = &spi;
    _pressed = false;
    _pressEventSent = false;
    _irqLowStreak = 0;
    _irqHighStreak = 0;
    resetFilter();

    pinMode(_cfg.csPin, OUTPUT);
    digitalWrite(_cfg.csPin, HIGH);
    pinMode(_cfg.tftCsPin, OUTPUT);
    digitalWrite(_cfg.tftCsPin, HIGH);

    if (_cfg.irqPin >= 0) {
        pinMode(_cfg.irqPin, INPUT);
    }

    if (!_cal.valid) {
        _cal.rawXMin = 300;
        _cal.rawXMax = 3700;
        _cal.rawYMin = 300;
        _cal.rawYMax = 3700;
        _cal.valid = true;
    }

    return true;
}

bool Xpt2046Touch::readRawSpiOnce(RawSample& out) const {
    if (!_spi) return false;

    prepareBusForTouch();

    SPISettings sp(_cfg.spiHz, MSBFIRST, SPI_MODE0);
    _spi->beginTransaction(sp);
    digitalWrite(_cfg.csPin, LOW);

    auto xfer = [this](uint8_t cmd) -> uint16_t {
        _spi->transfer(cmd);
        uint16_t v = (uint16_t)_spi->transfer(0x00) << 8;
        v |= _spi->transfer(0x00);
        return v >> 3;
    };

    out.x = xfer(0xD0);
    const uint16_t z1 = xfer(0xB0);
    out.y = xfer(0x90);
    const uint16_t z2 = xfer(0xC0);

    digitalWrite(_cfg.csPin, HIGH);
    _spi->endTransaction();

    const int z = (int)z1 + 4095 - (int)z2;
    out.z = (z < 0) ? 0 : (uint16_t)z;
    out.irqLow = irqLow();
    return true;
}

bool Xpt2046Touch::sampleValid(const RawSample& s) const {
    if (s.z < _cfg.zThreshold) return false;
    if (s.x <= 80 || s.y <= 80) return false;
    if (s.x >= kSaturation || s.y >= kSaturation) return false;
    if ((s.x < 100 && s.y > 3900) || (s.y < 100 && s.x > 3900)) return false;
    return true;
}

bool Xpt2046Touch::readRawSpiAvg(RawSample& out) const {
    uint32_t sumX = 0;
    uint32_t sumY = 0;
    uint32_t sumZ = 0;
    uint8_t n = 0;

    const uint8_t kN = _cfg.spiAvgSamples < 1 ? 1 : _cfg.spiAvgSamples;
    for (uint8_t i = 0; i < kN; i++) {
        RawSample s{};
        if (!readRawSpiOnce(s) || !sampleValid(s)) continue;
        sumX += s.x;
        sumY += s.y;
        sumZ += s.z;
        n++;
    }

    if (n == 0) return false;

    out.x = (uint16_t)(sumX / n);
    out.y = (uint16_t)(sumY / n);
    out.z = (uint16_t)(sumZ / n);
    out.irqLow = irqLow();
    return true;
}

bool Xpt2046Touch::mapRaw(int16_t rawX, int16_t rawY, int16_t& mappedX, int16_t& mappedY) const {
    if (!_cal.valid) return false;

    const int32_t rw = _cal.rawXMax - _cal.rawXMin;
    const int32_t rh = _cal.rawYMax - _cal.rawYMin;
    if (rw < 50 || rh < 50) return false;

    int32_t dx = (int32_t)rawX - _cal.rawXMin;
    int32_t dy = (int32_t)rawY - _cal.rawYMin;
    if (dx < 0) dx = 0;
    if (dy < 0) dy = 0;
    if (dx > rw) dx = rw;
    if (dy > rh) dy = rh;

    mappedX = (int16_t)((dx * (_cfg.screenW - 1)) / rw);
    mappedY = (int16_t)((dy * (_cfg.screenH - 1)) / rh);
    return true;
}

void Xpt2046Touch::applyRotation(int16_t mappedX, int16_t mappedY, int16_t& outX, int16_t& outY) const {
    const int w = _cfg.screenW;
    const int h = _cfg.screenH;

    switch (_rotation) {
        case Rotation::R180:
            outX = (int16_t)(w - 1 - mappedX);
            outY = (int16_t)(h - 1 - mappedY);
            break;
        case Rotation::R90:
            outX = (int16_t)((int32_t)mappedY * (w - 1) / (h - 1));
            outY = (int16_t)((int32_t)(w - 1 - mappedX) * (h - 1) / (w - 1));
            break;
        case Rotation::R270:
            outX = (int16_t)((int32_t)(h - 1 - mappedY) * (w - 1) / (h - 1));
            outY = (int16_t)((int32_t)mappedX * (h - 1) / (w - 1));
            break;
        case Rotation::R0:
        default:
            outX = mappedX;
            outY = mappedY;
            break;
    }

    outX = clamp16(outX, 0, w - 1);
    outY = clamp16(outY, 0, h - 1);
}

bool Xpt2046Touch::applyEmaAndDeadzone(int16_t mappedX, int16_t mappedY, int16_t& outX, int16_t& outY) {
    const int32_t alpha = _cfg.emaAlpha;
    const int32_t inv = 256 - alpha;

    if (!_emaInit) {
        _emaX = (int32_t)mappedX * 256;
        _emaY = (int32_t)mappedY * 256;
        _emaInit = true;
        _stableX = mappedX;
        _stableY = mappedY;
        outX = mappedX;
        outY = mappedY;
        return true;
    }

    _emaX = (alpha * mappedX + inv * _emaX) / 256;
    _emaY = (alpha * mappedY + inv * _emaY) / 256;

    const int16_t fx = (int16_t)((_emaX + 128) / 256);
    const int16_t fy = (int16_t)((_emaY + 128) / 256);

    const int16_t dx = (int16_t)abs((int)fx - (int)_stableX);
    const int16_t dy = (int16_t)abs((int)fy - (int)_stableY);

    if (dx >= _cfg.deadzonePx || dy >= _cfg.deadzonePx) {
        _stableX = fx;
        _stableY = fy;
    }

    outX = _stableX;
    outY = _stableY;
    return true;
}

bool Xpt2046Touch::getPointEx(TouchPoint& pt) {
    pt = {};
    const bool wasPressed = _pressed;
    updateIrqDebounce();

    if (irqGated(_cfg) && !_pressed) {
        if (wasPressed) {
            appLog(APP_LOG_INFO, "TCH", "event=released irq=HIGH");
        }
        pt.pressed = false;
        return false;
    }
    if (!irqGated(_cfg) && !irqLow()) {
        pt.pressed = false;
        return false;
    }

    RawSample s{};
    if (!readRawSpiAvg(s)) {
        pt.pressed = false;
        return false;
    }

    pt.rawX = (int16_t)s.x;
    pt.rawY = (int16_t)s.y;

    int16_t mx = 0, my = 0;
    if (!mapRaw(pt.rawX, pt.rawY, mx, my)) {
        pt.pressed = false;
        return false;
    }
    pt.mappedX = mx;
    pt.mappedY = my;

    int16_t rx = 0, ry = 0;
    applyRotation(mx, my, rx, ry);

    int16_t fx = rx, fy = ry;
    applyEmaAndDeadzone(rx, ry, fx, fy);

    pt.filtX = fx;
    pt.filtY = fy;
    pt.x = fx;
    pt.y = fy;
    pt.pressed = true;

    if (!wasPressed && _pressed && !_pressEventSent) {
        _pressEventSent = true;
        appLogf(APP_LOG_INFO, "TCH",
                "event=pressed raw=%d,%d map=%d,%d out=%d,%d rot=%u",
                pt.rawX, pt.rawY, pt.mappedX, pt.mappedY, pt.x, pt.y, (unsigned)_rotation);
    }

    return true;
}

bool Xpt2046Touch::getPoint(int16_t& x, int16_t& y) {
    TouchPoint pt{};
    if (!getPointEx(pt)) return false;
    x = pt.x;
    y = pt.y;
    return true;
}

bool Xpt2046Touch::captureCalibrationCorner(int corner, uint16_t& rawX, uint16_t& rawY) {
    if (corner < 0 || corner > 3) return false;
    if (!irqLow()) return false;

    uint32_t sumX = 0;
    uint32_t sumY = 0;
    uint8_t n = 0;
    for (uint8_t i = 0; i < 16; i++) {
        RawSample s{};
        if (!readRawSpiOnce(s) || !sampleValid(s)) continue;
        sumX += s.x;
        sumY += s.y;
        n++;
        delay(5);
    }
    if (n < 8) return false;

    rawX = (uint16_t)(sumX / n);
    rawY = (uint16_t)(sumY / n);
    return true;
}

void Xpt2046Touch::commitCalibrationFromCorners(const uint16_t rawX[4], const uint16_t rawY[4]) {
    Calibration c{};
    c.rawXMin = (int32_t)rawX[0];
    if ((int32_t)rawX[3] < c.rawXMin) c.rawXMin = rawX[3];
    c.rawXMax = (int32_t)rawX[1];
    if ((int32_t)rawX[2] > c.rawXMax) c.rawXMax = rawX[2];

    c.rawYMin = (int32_t)rawY[0];
    if ((int32_t)rawY[1] < c.rawYMin) c.rawYMin = rawY[1];
    c.rawYMax = (int32_t)rawY[3];
    if ((int32_t)rawY[2] > c.rawYMax) c.rawYMax = rawY[2];

    if (c.rawXMin > c.rawXMax) {
        int32_t t = c.rawXMin;
        c.rawXMin = c.rawXMax;
        c.rawXMax = t;
    }
    if (c.rawYMin > c.rawYMax) {
        int32_t t = c.rawYMin;
        c.rawYMin = c.rawYMax;
        c.rawYMax = t;
    }

    c.valid = true;
    setCalibration(c);

    appLogf(APP_LOG_INFO, "TCH",
            "calibration x=[%ld,%ld] y=[%ld,%ld]",
            (long)c.rawXMin, (long)c.rawXMax, (long)c.rawYMin, (long)c.rawYMax);
}

bool Xpt2046Touch::runDiagnostics(const char* tag) {
    const char* comp = tag ? tag : "TCH";
    if (irqGated(_cfg) && !irqLow()) {
        appLog(APP_LOG_INFO, comp, "diag idle (IRQ HIGH)");
        return true;
    }
    TouchPoint pt{};
    if (getPointEx(pt)) {
        appLogf(APP_LOG_INFO, comp,
                 "diag raw=%d,%d map=%d,%d out=%d,%d rot=%u",
                 pt.rawX, pt.rawY, pt.mappedX, pt.mappedY, pt.x, pt.y, (unsigned)_rotation);
    }
    return true;
}

#if defined(MINI_AZAN_TOUCH_DEBUG) && MINI_AZAN_TOUCH_DEBUG
void Xpt2046Touch::drawDebugMarker(int16_t x, int16_t y, bool pressed) {
    (void)x;
    (void)y;
    (void)pressed;
}
#endif
