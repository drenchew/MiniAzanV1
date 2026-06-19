#pragma once

#include "BoardConfigPins.h"

/**
 * MiniAzan hardware profile — C++ pin constants derived from BoardConfigPins.h.
 *
 * ESP32-S3 N16R8: FSPI=SD, HSPI=TFT+touch. GPIO 26–37 reserved on OPI PSRAM modules.
 * See BoardConfigPins.h to change wiring (also update User_Setup.h macros there).
 */

namespace BoardConfig {

#if MINI_AZAN_HAS_PSRAM
constexpr const char* kTargetName = "ESP32-S3 N16R8";
#else
constexpr const char* kTargetName = "ESP32 WROOM";
#endif

constexpr int SD_SCK  = MINI_AZAN_SD_SCK;
constexpr int SD_MISO = MINI_AZAN_SD_MISO;
constexpr int SD_MOSI = MINI_AZAN_SD_MOSI;
constexpr int SD_CS   = MINI_AZAN_SD_CS;

constexpr int UI_SCK       = MINI_AZAN_TFT_SCLK;
constexpr int UI_MISO      = MINI_AZAN_TFT_MISO;
constexpr int UI_MOSI      = MINI_AZAN_TFT_MOSI;
constexpr int UI_TFT_CS    = MINI_AZAN_TFT_CS;
constexpr int UI_TFT_DC    = MINI_AZAN_TFT_DC;
constexpr int UI_TFT_RST   = MINI_AZAN_TFT_RST;
constexpr int UI_TOUCH_CS  = MINI_AZAN_TOUCH_CS;
constexpr int UI_TOUCH_IRQ = MINI_AZAN_TOUCH_IRQ;

constexpr int I2S_BCLK = MINI_AZAN_I2S_BCLK;
constexpr int I2S_LRC  = MINI_AZAN_I2S_LRC;
constexpr int I2S_DOUT = MINI_AZAN_I2S_DOUT;

constexpr int I2C_SDA = MINI_AZAN_I2C_SDA;
constexpr int I2C_SCL = MINI_AZAN_I2C_SCL;

}  // namespace BoardConfig
