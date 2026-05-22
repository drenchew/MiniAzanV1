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
constexpr int UI_TOUCH_CS = 33;

SPIClass& sdSpi();
SPIClass& uiSpi();

}  // namespace SpiArch
