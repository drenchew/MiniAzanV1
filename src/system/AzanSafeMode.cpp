#include "system/AzanSafeMode.h"
#include "system/SchedPriority.h"
#include "AppLog.h"

namespace AzanSafeMode {

static volatile bool s_active = false;

void enter(const char* reason) {
    if (s_active) {
        return;
    }
    s_active = true;
    appLogf(APP_LOG_INFO, "SAFE", "azan_lock ON reason=%s",
            reason ? reason : "audio");
}

void exit() {
    if (!s_active) {
        return;
    }
    s_active = false;
    appLog(APP_LOG_INFO, "SAFE", "azan_lock OFF");
}

bool isActive() {
    return s_active;
}

bool isLocked() {
    return s_active;
}

bool allowPriority(uint8_t priorityBand) {
    if (!s_active) {
        return true;
    }
    return priorityBand <= SchedPriority::P1_RealTime;
}

bool allowStorageJobs() {
    return !s_active;
}

bool allowBluetoothTransfer() {
    return !s_active;
}

bool allowLvglTickRefresh() {
    return !s_active;
}

}  // namespace AzanSafeMode
