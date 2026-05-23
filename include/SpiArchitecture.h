#pragma once

#include <SPI.h>

/**
 * SPI bus ownership (mandatory for this project)
 * -----------------------------------------------
 * BUS 1 (VSPI): SD card ONLY — StorageManager + AudioManager (via StorageManager)
 * BUS 2 (HSPI): TFT + touch ONLY — UIManager (future; must not touch SD)
 *
 * Rules:
 * - Never call SD.begin() outside StorageManager.
 * - Never use SPI / HSPI from main, web handlers, or future UI for SD.
 * - ESP32-audioI2S reads SD only through connecttoFS(StorageManager::mediaFs(), ...).
 * - UiSpi / HSPI must not be started until UIManager is integrated.
 */
namespace SpiArch {

constexpr int SD_HOST = VSPI;
constexpr int UI_HOST = HSPI;

// VSPI — SD card (must match wiring)
constexpr int SD_SCK  = 18;
constexpr int SD_MISO = 19;
constexpr int SD_MOSI = 23;
constexpr int SD_CS   = 5;

// HSPI — reserved for TFT + touch (UIManager only; do not use for SD)
constexpr int UI_SCK  = 14;
constexpr int UI_MISO = 12;
constexpr int UI_MOSI = 13;
constexpr int UI_TFT_CS   = 15;
constexpr int UI_TFT_DC   = 2;
constexpr int UI_TFT_RST  = 32;
constexpr int UI_TOUCH_CS = 33;
/** Optional XPT2046 pen IRQ (input-only GPIO). Set -1 to disable. */
constexpr int UI_TOUCH_IRQ = 34;

struct BusInitResult {
    bool ok = false;
    int hostId = -1;  // ESP32: VSPI=3 HSPI=2 (Arduino SPIClass::bus())
    int sck = 0;
    int miso = 0;
    int mosi = 0;
};

SPIClass& sdSpi();
SPIClass& uiSpi();

/** VSPI: SD only — call once before SD.begin(). */
BusInitResult initSdBus();

/** HSPI: TFT + touch — call once before tft.init() / touch.begin(). */
BusInitResult initUiBus();

/** Drive TFT CS high so XPT2046 can use shared MISO (required during touch SPI). */
void releaseTftChipSelect();

}  // namespace SpiArch
