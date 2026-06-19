#pragma once

#include <SPI.h>
#include "BoardConfig.h"

#if MINI_AZAN_HAS_PSRAM
#include <driver/spi_master.h>
#endif

/**
 * SPI bus ownership (mandatory for this project)
 * -----------------------------------------------
 * ESP32:     VSPI = SD only,  HSPI = TFT + touch
 * ESP32-S3:  FSPI = SD only,  HSPI = TFT + touch  (separate physical buses)
 *
 * Rules:
 * - Never call SD.begin() outside StorageManager.
 * - Never use the UI SPI bus for SD access.
 * - ESP32-audioI2S reads SD only through connecttoFS(StorageManager::mediaFs(), ...).
 */
namespace SpiArch {

#if MINI_AZAN_HAS_PSRAM
constexpr int SD_HOST = SPI2_HOST;   // FSPI
constexpr int UI_HOST = SPI3_HOST;   // HSPI
#else
constexpr int SD_HOST = VSPI;
constexpr int UI_HOST = HSPI;
#endif

constexpr int SD_SCK  = BoardConfig::SD_SCK;
constexpr int SD_MISO = BoardConfig::SD_MISO;
constexpr int SD_MOSI = BoardConfig::SD_MOSI;
constexpr int SD_CS   = BoardConfig::SD_CS;

constexpr int UI_SCK       = BoardConfig::UI_SCK;
constexpr int UI_MISO      = BoardConfig::UI_MISO;
constexpr int UI_MOSI      = BoardConfig::UI_MOSI;
constexpr int UI_TFT_CS    = BoardConfig::UI_TFT_CS;
constexpr int UI_TFT_DC    = BoardConfig::UI_TFT_DC;
constexpr int UI_TFT_RST   = BoardConfig::UI_TFT_RST;
constexpr int UI_TOUCH_CS  = BoardConfig::UI_TOUCH_CS;
/** XPT2046 pen IRQ (active LOW). Set -1 to disable. */
constexpr int UI_TOUCH_IRQ = BoardConfig::UI_TOUCH_IRQ;

struct BusInitResult {
    bool ok = false;
    int hostId = -1;
    int sck = 0;
    int miso = 0;
    int mosi = 0;
};

SPIClass& sdSpi();
SPIClass& uiSpi();

/** SD bus — call once before SD.begin(). */
BusInitResult initSdBus();

/** TFT + touch bus — call once before tft.init() / touch.begin(). */
BusInitResult initUiBus();

/** Drive TFT CS high so XPT2046 can use shared MISO during touch SPI. */
void releaseTftChipSelect();

}  // namespace SpiArch
