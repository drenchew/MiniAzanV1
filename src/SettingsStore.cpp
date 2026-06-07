#include "SettingsStore.h"
#include <nvs_flash.h>

bool SettingsStore::begin() {
    return true;
}

static bool openRw(nvs_handle_t& h) {
    return nvs_open(SettingsStore::kNs, NVS_READWRITE, &h) == ESP_OK;
}

static bool openRo(nvs_handle_t& h) {
    return nvs_open(SettingsStore::kNs, NVS_READONLY, &h) == ESP_OK;
}

bool SettingsStore::loadVolume(uint8_t& out, uint8_t defaultVal) {
    nvs_handle_t h;
    if (!openRo(h)) {
        out = defaultVal;
        return false;
    }
    uint8_t v = defaultVal;
    esp_err_t e = nvs_get_u8(h, SettingsStore::kVolume, &v);
    nvs_close(h);
    out = v;
    return e == ESP_OK;
}

bool SettingsStore::saveVolume(uint8_t v) {
    nvs_handle_t h;
    if (!openRw(h)) return false;
    nvs_set_u8(h, SettingsStore::kVolume, v);
    nvs_commit(h);
    nvs_close(h);
    return true;
}

bool SettingsStore::loadPreFajr(bool& out, bool defaultVal) {
    nvs_handle_t h;
    if (!openRo(h)) {
        out = defaultVal;
        return false;
    }
    uint8_t v = defaultVal ? 1 : 0;
    esp_err_t e = nvs_get_u8(h, SettingsStore::kPreFajr, &v);
    nvs_close(h);
    out = (v != 0);
    return e == ESP_OK;
}

bool SettingsStore::savePreFajr(bool on) {
    nvs_handle_t h;
    if (!openRw(h)) return false;
    nvs_set_u8(h, SettingsStore::kPreFajr, on ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
    return true;
}

bool SettingsStore::loadAzanIndex(uint8_t& out, uint8_t maxIdx) {
    nvs_handle_t h;
    if (!openRo(h)) return false;
    uint8_t v = 0;
    esp_err_t e = nvs_get_u8(h, SettingsStore::kAzanIdx, &v);
    nvs_close(h);
    if (e == ESP_OK && v < maxIdx) {
        out = v;
        return true;
    }
    return false;
}

bool SettingsStore::saveAzanIndex(uint8_t idx) {
    nvs_handle_t h;
    if (!openRw(h)) return false;
    nvs_set_u8(h, SettingsStore::kAzanIdx, idx);
    nvs_commit(h);
    nvs_close(h);
    return true;
}

bool SettingsStore::loadAzanPath(char* out, size_t outLen) {
    if (!out || outLen < 2) return false;
    nvs_handle_t h;
    if (!openRo(h)) return false;
    size_t len = outLen;
    esp_err_t e = nvs_get_str(h, SettingsStore::kAzanPath, out, &len);
    nvs_close(h);
    return e == ESP_OK && out[0];
}

bool SettingsStore::saveAzanPath(const char* path) {
    if (!path) return false;
    nvs_handle_t h;
    if (!openRw(h)) return false;
    nvs_set_str(h, SettingsStore::kAzanPath, path);
    nvs_commit(h);
    nvs_close(h);
    return true;
}

bool SettingsStore::loadWifiLastState(bool& out, bool defaultVal) {
    nvs_handle_t h;
    if (!openRo(h)) {
        out = defaultVal;
        return false;
    }
    uint8_t v = defaultVal ? 1 : 0;
    esp_err_t e = nvs_get_u8(h, SettingsStore::kWifiLast, &v);
    nvs_close(h);
    out = (v != 0);
    return e == ESP_OK;
}

bool SettingsStore::saveWifiLastState(bool on) {
    nvs_handle_t h;
    if (!openRw(h)) return false;
    nvs_set_u8(h, SettingsStore::kWifiLast, on ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
    return true;
}
