#pragma once

/**
 * Hardware pin macros (C-safe for TFT_eSPI User_Setup.h).
 *
 * AUX analog output (ESP32 only):
 *   GPIO25 (DAC1) → series 100µF cap (+) → AUX tip
 *   GND → AUX sleeve
 * ESP32-S3 has no built-in DAC — use an I2S DAC module (e.g. PCM5102) instead.
 */

#if defined(MINI_AZAN_TARGET_S3) && MINI_AZAN_TARGET_S3

#define MINI_AZAN_HAS_PSRAM 1
#define MINI_AZAN_AUDIO_AUX_DAC 0

#define MINI_AZAN_I2S_BCLK  4
#define MINI_AZAN_I2S_LRC   5
#define MINI_AZAN_I2S_DOUT  42

#else

#define MINI_AZAN_HAS_PSRAM 0
/** 1 = built-in DAC to AUX (GPIO25), 0 = external I2S (3-wire DAC/amp) */
#ifndef MINI_AZAN_AUDIO_AUX_DAC
#define MINI_AZAN_AUDIO_AUX_DAC 1
#endif

#if MINI_AZAN_AUDIO_AUX_DAC
#define MINI_AZAN_AUX_DAC_GPIO 25
#else
#define MINI_AZAN_I2S_BCLK  26
#define MINI_AZAN_I2S_LRC   25
#define MINI_AZAN_I2S_DOUT  22
#endif

#endif
