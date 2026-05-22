#pragma once

#include "SpiArchitecture.h"

/**
 * Future TFT + touch UI (HSPI only).
 *
 * - Use SpiArch::uiSpi() — never SpiArch::sdSpi() or SD.h
 * - Never call StorageManager; request data via app callbacks
 */
class UIManager {
public:
    bool begin() { return false; }  // not integrated yet
};
