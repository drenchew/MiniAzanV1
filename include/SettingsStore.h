#pragma once

#include <Arduino.h>
#include <nvs.h>

/** NVS persistence for UI settings (namespace: azan_system). */
class SettingsStore {
public:
    static constexpr const char* kNs = "azan_system";
    static constexpr const char* kVolume = "volume";
    static constexpr const char* kPreFajr = "prefajr";
    static constexpr const char* kAzanIdx = "azan_idx";
    static constexpr const char* kAzanPath = "azan_path";
    static constexpr const char* kWifiLast = "wifi_last";

    bool begin();
    bool loadVolume(uint8_t& out, uint8_t defaultVal = 15);
    bool saveVolume(uint8_t v);
    bool loadPreFajr(bool& out, bool defaultVal = false);
    bool savePreFajr(bool on);
    bool loadAzanIndex(uint8_t& out, uint8_t maxIdx);
    bool saveAzanIndex(uint8_t idx);
    bool loadAzanPath(char* out, size_t outLen);
    bool saveAzanPath(const char* path);
    bool loadWifiLastState(bool& out, bool defaultVal = true);
    bool saveWifiLastState(bool on);
};
