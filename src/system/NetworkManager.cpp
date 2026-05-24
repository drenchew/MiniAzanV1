#include "system/NetworkManager.h"
#include "system/AzanSafeMode.h"
#include "TimeManager.h"
#include "AppLog.h"
#include <WiFi.h>

void NetworkManager::notifyState() {
    if (_onState) {
        _onState(_wifiOn, _onStateUser);
    }
}

void NetworkManager::setInitialConnected(bool on) {
    _wifiOn = on;
    _state = on ? State::On : State::Off;
    if (on && _time) {
        _time->setWifiConnected(true);
    }
    notifyState();
}

bool NetworkManager::begin(AsyncWebServer& server, TimeManager& time, const Config& cfg,
                          StateCallback onState, void* user) {
    _server = &server;
    _time = &time;
    _cfg = cfg;
    _onState = onState;
    _onStateUser = user;
    _state = State::Off;
    _wifiOn = false;
    return _server != nullptr && _cfg.ssid != nullptr;
}

bool NetworkManager::isConnected() const {
    return _wifiOn && WiFi.status() == WL_CONNECTED;
}

void NetworkManager::logState(const char* msg) const {
    appLogf(APP_LOG_INFO, "NET", "%s state=%u wifiOn=%d",
            msg, (unsigned)_state, _wifiOn ? 1 : 0);
}

bool NetworkManager::requestToggle() {
    _pendingToggle = true;
    return true;
}

bool NetworkManager::requestStop() {
    _pendingStop = true;
    _pendingStart = false;
    _pendingToggle = false;
    return true;
}

bool NetworkManager::requestStart() {
    if (!AzanSafeMode::allowWifiStart()) {
        appLog(APP_LOG_WARN, "NET", "WiFi start blocked (azan safe mode)");
        return false;
    }
    _pendingStart = true;
    _pendingStop = false;
    _pendingToggle = false;
    return true;
}

void NetworkManager::stepStop() {
    if (_server) {
        _server->end();
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    _wifiOn = false;
    _state = State::Off;
    if (_time) {
        _time->setWifiConnected(false);
    }
    logState("stopped");
    notifyState();
}

void NetworkManager::stepStart() {
    if (!AzanSafeMode::allowWifiStart()) {
        _pendingStart = false;
        _state = State::Off;
        return;
    }

    const uint32_t now = millis();
    if (_state == State::Off) {
        appLog(APP_LOG_INFO, "NET", "WiFi starting (async)");
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.begin(_cfg.ssid, _cfg.password);
        _attempt = 0;
        _state = State::Starting;
        _stepMs = now;
        return;
    }

    if (_state != State::Starting) {
        return;
    }

    if (now - _stepMs < _cfg.attemptDelayMs) {
        return;
    }
    _stepMs = now;

    if (WiFi.status() == WL_CONNECTED) {
        _wifiOn = true;
        _state = State::On;
        if (_server) {
            _server->begin();
        }
        if (_time) {
            _time->setWifiConnected(true);
            _time->requestNtpSync();
        }
        appLogf(APP_LOG_INFO, "NET", "WiFi connected ip=%s",
                WiFi.localIP().toString().c_str());
        _pendingStart = false;
        notifyState();
        return;
    }

    _attempt++;
    if (_attempt >= (uint8_t)_cfg.connectAttempts) {
        appLog(APP_LOG_WARN, "NET", "WiFi connect failed — staying off");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        _wifiOn = false;
        _state = State::Off;
        _pendingStart = false;
        notifyState();
    }
}

void NetworkManager::poll() {
    if (_pendingToggle) {
        _pendingToggle = false;
        if (_wifiOn || _state == State::On || _state == State::Starting) {
            _pendingStop = true;
        } else {
            _pendingStart = true;
        }
    }

    if (_pendingStop) {
        _pendingStop = false;
        stepStop();
        return;
    }

    if (_pendingStart) {
        stepStart();
    }
}
