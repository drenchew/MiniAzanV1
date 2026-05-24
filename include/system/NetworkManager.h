#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "freertos/queue.h"

class TimeManager;

/**
 * WiFi / AsyncTCP lifecycle — never blocks LVGL; polled from SystemCoordinator.
 * Does not start WiFi during AzanSafeMode.
 */
class NetworkManager {
public:
    struct Config {
        const char* ssid = nullptr;
        const char* password = nullptr;
        int connectAttempts = 15;
        uint32_t attemptDelayMs = 500;
    };

    using StateCallback = void (*)(bool wifiOn, void* user);
    bool begin(AsyncWebServer& server, TimeManager& time, const Config& cfg,
               StateCallback onState = nullptr, void* user = nullptr);
    void poll();

    bool isOn() const { return _wifiOn; }
    bool isConnected() const;
    bool requestToggle();
    bool requestStop();
    bool requestStart();

    /** After blocking boot connect in setup(). */
    void setInitialConnected(bool on);

private:
    enum class State : uint8_t {
        Off = 0,
        Stopping,
        Starting,
        On,
    };

    void stepStop();
    void stepStart();
    void logState(const char* msg) const;

    AsyncWebServer* _server = nullptr;
    TimeManager* _time = nullptr;
    Config _cfg{};
    State _state = State::Off;
    bool _wifiOn = false;
    uint8_t _attempt = 0;
    uint32_t _stepMs = 0;
    bool _pendingToggle = false;
    bool _pendingStart = false;
    bool _pendingStop = false;
    StateCallback _onState = nullptr;
    void* _onStateUser = nullptr;

    void notifyState();
};
