#pragma once

#include <SPI.h>

/**
 * XPT2046 on shared HSPI. PENIRQ active-LOW; GPIO34 needs external 10k to 3.3V.
 */
class Xpt2046Touch {
public:
    enum class Rotation : uint8_t { R0 = 0, R90 = 1, R180 = 2, R270 = 3 };

    struct Calibration {
        int32_t rawXMin = 300;
        int32_t rawXMax = 3700;
        int32_t rawYMin = 300;
        int32_t rawYMax = 3700;
        bool valid = false;
    };

    struct Config {
        int csPin = 33;
        int tftCsPin = 15;
        int sck = 14;
        int miso = 12;
        int mosi = 13;
        int irqPin = 34;
        uint32_t spiHz = 2000000;
        int screenW = 240;
        int screenH = 320;
        uint16_t zThreshold = 200;
        uint8_t pressDebounce = 3;
        uint8_t releaseDebounce = 3;
        uint8_t spiAvgSamples = 3;
        uint8_t deadzonePx = 4;
        /** EMA weight for new sample (0–255). Higher = less smoothing. */
        uint8_t emaAlpha = 96;
    };

    struct RawSample {
        uint16_t x = 0;
        uint16_t y = 0;
        uint16_t z = 0;
        bool irqLow = false;
    };

    struct TouchPoint {
        bool pressed = false;
        int16_t x = 0;
        int16_t y = 0;
        int16_t rawX = 0;
        int16_t rawY = 0;
        int16_t filtX = 0;
        int16_t filtY = 0;
        int16_t mappedX = 0;
        int16_t mappedY = 0;
    };

    bool begin(const Config& cfg, SPIClass& spi);
    bool isReady() const { return _spi != nullptr; }
    bool touched() const { return _pressed; }
    bool irqLow() const;

    void setRotation(Rotation r) { _rotation = r; }
    Rotation rotation() const { return _rotation; }

    void setCalibration(const Calibration& cal);
    const Calibration& calibration() const { return _cal; }

    /** Map raw ADC to screen pixels (before rotation). */
    bool mapRaw(int16_t rawX, int16_t rawY, int16_t& mappedX, int16_t& mappedY) const;

    void applyRotation(int16_t mappedX, int16_t mappedY, int16_t& outX, int16_t& outY) const;

    /**
     * Full pipeline: IRQ gate → SPI avg → map → rotate → EMA → deadzone.
     * false = finger up (filter state cleared).
     */
    bool getPoint(int16_t& x, int16_t& y);
    bool getPointEx(TouchPoint& out);

    /** Corner index 0..3: TL, TR, BR, BL. Returns false if sample invalid. */
    bool captureCalibrationCorner(int corner, uint16_t& rawX, uint16_t& rawY);
    void commitCalibrationFromCorners(const uint16_t rawX[4], const uint16_t rawY[4]);

    bool runDiagnostics(const char* tag = "TCH");
    void resetFilter();

#if defined(MINI_AZAN_TOUCH_DEBUG) && MINI_AZAN_TOUCH_DEBUG
    void drawDebugMarker(int16_t x, int16_t y, bool pressed);
#endif

private:
    bool readRawSpiOnce(RawSample& out) const;
    bool readRawSpiAvg(RawSample& out) const;
    bool sampleValid(const RawSample& s) const;
    void updateIrqDebounce();
    bool applyEmaAndDeadzone(int16_t mappedX, int16_t mappedY, int16_t& outX, int16_t& outY);
    void onRelease();
    void prepareBusForTouch() const;

    Config _cfg{};
    Calibration _cal{};
    SPIClass* _spi = nullptr;
    Rotation _rotation = Rotation::R0;

    bool _pressed = false;
    bool _pressEventSent = false;
    uint8_t _irqLowStreak = 0;
    uint8_t _irqHighStreak = 0;

    bool _emaInit = false;
    int32_t _emaX = 0;
    int32_t _emaY = 0;
    int16_t _stableX = 0;
    int16_t _stableY = 0;
};
