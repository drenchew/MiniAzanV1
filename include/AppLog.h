#pragma once

#include <Arduino.h>

class TimeManager;

enum AppLogLevel : uint8_t {
    APP_LOG_ERROR = 0,
    APP_LOG_WARN  = 1,
    APP_LOG_INFO  = 2,
    APP_LOG_DEBUG = 3
};

void appLogInit(TimeManager* timeMgr);
void appLog(int level, const char* component, const char* message);
void appLogf(int level, const char* component, const char* fmt, ...);
