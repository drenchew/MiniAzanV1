#include "ui/Xpt2046Touch.h"
#include <Arduino.h>

bool Xpt2046Touch::begin(const Config& cfg, SPIClass& spi) {
    _cfg = cfg;
    _spi = &spi;
    pinMode(_cfg.csPin, OUTPUT);
    digitalWrite(_cfg.csPin, HIGH);
    return true;
}

uint16_t Xpt2046Touch::readRaw(uint8_t command) const {
    if (!_spi) return 0;
    SPISettings sp(_cfg.spiHz, MSBFIRST, SPI_MODE0);
    _spi->beginTransaction(sp);
    digitalWrite(_cfg.csPin, LOW);
    _spi->transfer(command);
    uint16_t v = (uint16_t)_spi->transfer(0x00) << 8;
    v |= _spi->transfer(0x00);
    digitalWrite(_cfg.csPin, HIGH);
    _spi->endTransaction();
    return v >> 3;
}

bool Xpt2046Touch::getPoint(int16_t& x, int16_t& y) {
    uint16_t rx = readRaw(0xD0);
    uint16_t ry = readRaw(0x90);
    if (rx < 50 && ry < 50) return false;

    int32_t dx = rx - _cfg.rawXMin;
    int32_t dy = ry - _cfg.rawYMin;
    int32_t rw = _cfg.rawXMax - _cfg.rawXMin;
    int32_t rh = _cfg.rawYMax - _cfg.rawYMin;
    if (rw <= 0 || rh <= 0) return false;

    x = (int16_t)((dx * _cfg.screenW) / rw);
    y = (int16_t)((dy * _cfg.screenH) / rh);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= _cfg.screenW) x = _cfg.screenW - 1;
    if (y >= _cfg.screenH) y = _cfg.screenH - 1;
    return true;
}
