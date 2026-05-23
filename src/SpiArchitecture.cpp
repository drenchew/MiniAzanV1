#include "SpiArchitecture.h"
#include <Arduino.h>

namespace SpiArch {

SPIClass& sdSpi() {
    static SPIClass bus(VSPI);
    return bus;
}

SPIClass& uiSpi() {
    static SPIClass bus(HSPI);
    return bus;
}

static BusInitResult beginSpiBus(SPIClass& spi, int hostLabel, int sck, int miso, int mosi) {
    BusInitResult r{};
    r.sck = sck;
    r.miso = miso;
    r.mosi = mosi;
    r.hostId = hostLabel;

    spi.begin(sck, miso, mosi);
    r.ok = true;
    return r;
}

BusInitResult initSdBus() {
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    return beginSpiBus(sdSpi(), SD_HOST, SD_SCK, SD_MISO, SD_MOSI);
}

BusInitResult initUiBus() {
    pinMode(UI_TFT_CS, OUTPUT);
    digitalWrite(UI_TFT_CS, HIGH);
    pinMode(UI_TOUCH_CS, OUTPUT);
    digitalWrite(UI_TOUCH_CS, HIGH);
    pinMode(UI_TFT_DC, OUTPUT);
    return beginSpiBus(uiSpi(), UI_HOST, UI_SCK, UI_MISO, UI_MOSI);
}

void releaseTftChipSelect() {
    digitalWrite(UI_TFT_CS, HIGH);
}

}  // namespace SpiArch
