#pragma once

/**
 * Pin macros only — safe for C preprocessor / TFT_eSPI User_Setup.h.
 * C++ code should include BoardConfig.h instead.
 */

#if defined(MINI_AZAN_TARGET_S3) && MINI_AZAN_TARGET_S3

#define MINI_AZAN_HAS_PSRAM 1

#define MINI_AZAN_SD_SCK    12
#define MINI_AZAN_SD_MISO   13
#define MINI_AZAN_SD_MOSI   11
#define MINI_AZAN_SD_CS     10

#define MINI_AZAN_TFT_CS    16
#define MINI_AZAN_TFT_DC    15
#define MINI_AZAN_TFT_RST   7
#define MINI_AZAN_TOUCH_CS  6
#define MINI_AZAN_TFT_MOSI  17
#define MINI_AZAN_TFT_SCLK  18
#define MINI_AZAN_TFT_MISO  8
#define MINI_AZAN_TOUCH_IRQ 9

#define MINI_AZAN_I2S_BCLK  4
#define MINI_AZAN_I2S_LRC   5
#define MINI_AZAN_I2S_DOUT  42

#define MINI_AZAN_I2C_SDA   21
#define MINI_AZAN_I2C_SCL   47

#else

#define MINI_AZAN_HAS_PSRAM 0

#define MINI_AZAN_SD_SCK    18
#define MINI_AZAN_SD_MISO   19
#define MINI_AZAN_SD_MOSI   23
#define MINI_AZAN_SD_CS     5

#define MINI_AZAN_TFT_CS    15
#define MINI_AZAN_TFT_DC    2
#define MINI_AZAN_TFT_RST   32
#define MINI_AZAN_TOUCH_CS  33
#define MINI_AZAN_TFT_MOSI  13
#define MINI_AZAN_TFT_SCLK  14
#define MINI_AZAN_TFT_MISO  12
#define MINI_AZAN_TOUCH_IRQ 34

#define MINI_AZAN_I2S_BCLK  26
#define MINI_AZAN_I2S_LRC   25
#define MINI_AZAN_I2S_DOUT  27

#define MINI_AZAN_I2C_SDA   21
#define MINI_AZAN_I2C_SCL   22

#endif
