#pragma once

#include <SPI.h>

/** XPT2046 on shared HSPI (separate CS from TFT). */
class Xpt2046Touch {
public:
    struct Config {
        int csPin = 33;
        int sck = 14;
        int miso = 12;
        int mosi = 13;
        uint32_t spiHz = 2500000;
        int rawXMin = 200;
        int rawXMax = 3800;
        int rawYMin = 200;
        int rawYMax = 3800;
        int screenW = 240;
        int screenH = 320;
    };

    bool begin(const Config& cfg, SPIClass& spi);
    bool getPoint(int16_t& x, int16_t& y);

private:
    uint16_t readRaw(uint8_t command) const;

    Config _cfg{};
    SPIClass* _spi = nullptr;
};
