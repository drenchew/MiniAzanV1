#pragma once

#include <stdbool.h>

/**
 * Exclusive resource mode while azan/I2S decode is active.
 * Blocks non-audio SD jobs, WiFi bring-up, and heavy UI refresh.
 */
namespace AzanSafeMode {

void enter(const char* reason);
void exit();
bool isActive();

bool allowStorageJobs();
bool allowWifiStart();
bool allowLvglTickRefresh();

}  // namespace AzanSafeMode
