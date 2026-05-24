#include "system/AzanSafeMode.h"
#include "AppLog.h"

namespace AzanSafeMode {

static volatile bool s_active = false;

void enter(const char* reason) {
    if (s_active) {
        return;
    }
    s_active = true;
    appLogf(APP_LOG_INFO, "SAFE", "azan_safe_mode ON reason=%s",
            reason ? reason : "audio");
}

void exit() {
    if (!s_active) {
        return;
    }
    s_active = false;
    appLog(APP_LOG_INFO, "SAFE", "azan_safe_mode OFF");
}

bool isActive() {
    return s_active;
}

bool allowStorageJobs() {
    return !s_active;
}

bool allowWifiStart() {
    return !s_active;
}

bool allowLvglTickRefresh() {
    return !s_active;
}

}  // namespace AzanSafeMode
