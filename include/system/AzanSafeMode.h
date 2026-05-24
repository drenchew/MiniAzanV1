#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * Azan safety lock — when active only P0/P1 work is permitted.
 * SD jobs pause at cooperative checkpoints; UI keeps rendering.
 */
namespace AzanSafeMode {

void enter(const char* reason);
void exit();
bool isActive();
bool isLocked();

bool allowPriority(uint8_t priorityBand);
bool allowStorageJobs();
bool allowBluetoothTransfer();
bool allowLvglTickRefresh();

}  // namespace AzanSafeMode
