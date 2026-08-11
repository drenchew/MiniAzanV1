#pragma once

#include "BoardConfigPins.h"

namespace BoardConfig {

#if MINI_AZAN_HAS_PSRAM
constexpr const char* kTargetName = "ESP32-S3 N16R8";
#else
constexpr const char* kTargetName = "ESP32 WROOM";
#endif

#if MINI_AZAN_AUDIO_AUX_DAC && !MINI_AZAN_HAS_PSRAM
constexpr bool kAudioAuxDac = true;
constexpr int I2S_BCLK = -1;
constexpr int I2S_LRC = -1;
constexpr int I2S_DOUT = MINI_AZAN_AUX_DAC_GPIO;
#else
constexpr bool kAudioAuxDac = false;
constexpr int I2S_BCLK = MINI_AZAN_I2S_BCLK;
constexpr int I2S_LRC = MINI_AZAN_I2S_LRC;
constexpr int I2S_DOUT = MINI_AZAN_I2S_DOUT;
#endif

}  // namespace BoardConfig
