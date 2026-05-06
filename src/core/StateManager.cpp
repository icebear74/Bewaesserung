#include "StateManager.h"

StateManager::StateManager() : _state(SystemState::BOOT) {}

void StateManager::setState(SystemState newState) {
    _state = newState;
    Serial.printf("[App] State -> %s\n", getStateString());
}

SystemState StateManager::getState() const {
    return _state;
}

const char* StateManager::getStateString() const {
    switch (_state) {
        case SystemState::BOOT:             return "Booting";
        case SystemState::LOADING_CONFIG:   return "Lade Konfig";
        case SystemState::WIFI_SCANNING:    return "WLAN suchen";
        case SystemState::WIFI_CONNECTING:  return "Verbinde";
        case SystemState::WIFI_WPS:         return "WPS...";
        case SystemState::WIFI_AP_MODE:     return "Setup-AP aktiv";
        case SystemState::WIFI_CONNECTED:   return "Verbunden";
        case SystemState::TIME_SYNC:        return "Zeitsync";
        case SystemState::RUNNING:          return "Betrieb";
        case SystemState::RUNNING_OFFLINE:  return "Offline-Betrieb";
        case SystemState::SETUP_REQUIRED:   return "Ersteinrichtung";
        case SystemState::RESTARTING:       return "Neustart";
        default:                            return "Unbekannt";
    }
}

bool StateManager::isRunning() const {
    return _state == SystemState::RUNNING || _state == SystemState::RUNNING_OFFLINE;
}

bool StateManager::isOfflineCapable() const {
    return _state == SystemState::RUNNING_OFFLINE;
}
