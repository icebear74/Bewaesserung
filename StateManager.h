#pragma once
#include <Arduino.h>

enum class SystemState {
    BOOT,
    LOADING_CONFIG,
    WIFI_SCANNING,
    WIFI_CONNECTING,
    WIFI_WPS,
    WIFI_AP_MODE,
    WIFI_CONNECTED,
    TIME_SYNC,
    RUNNING,
    RUNNING_OFFLINE,
    SETUP_REQUIRED,
    RESTARTING
};

class StateManager {
public:
    StateManager();
    void setState(SystemState newState);
    SystemState getState() const;
    const char* getStateString() const;
    bool isRunning() const;
    bool isOfflineCapable() const;

private:
    SystemState _state;
};
