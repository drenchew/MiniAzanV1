#include "AppLog.h"
#include "TimeManager.h"
#include <stdarg.h>

static TimeManager* s_time = nullptr;

void appLogInit(TimeManager* timeMgr) {
    s_time = timeMgr;
}

static void printPrefix(int level, const char* component) {
    const char* lvl;
    switch (level) {
        case APP_LOG_ERROR: lvl = "E"; break;
        case APP_LOG_WARN:  lvl = "W"; break;
        case APP_LOG_INFO:  lvl = "I"; break;
        default:            lvl = "D"; break;
    }

    unsigned long up = millis();
    PrayerNow now{};
    if (s_time && s_time->getPrayerNow(now) && now.valid) {
        Serial.printf("%lu|%04d-%02d-%02d %02d:%02d:%02d|%s|%-4s|",
                      up,
                      now.year, now.month, now.day,
                      now.hour, now.minute, now.second,
                      lvl, component);
    } else {
        Serial.printf("%lu|-------- --:--:--|%s|%-4s|", up, lvl, component);
    }
}

void appLog(int level, const char* component, const char* message) {
    printPrefix(level, component);
    Serial.println(message);
}

void appLogf(int level, const char* component, const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    appLog(level, component, buf);
}
