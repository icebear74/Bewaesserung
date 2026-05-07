#include "Application.h"
#include "StateManager.h"
#include "ConfigManager.h"
#include "WifiManager.h"
#include "TimeSync.h"
#include "OledStatus.h"
#include "Ds3231Manager.h"
#include "RelayManager.h"
#include "WebServerManager.h"
#include <Wire.h>

Application::Application() {}

Application::~Application() {
    delete _stateManager;
    delete _configManager;
    delete _wifiManager;
    delete _timeSync;
    delete _oledStatus;
    delete _ds3231;
    delete _relayManager;
    delete _webServer;
}

void Application::begin() {
    // 1. Boot state
    _stateManager = new StateManager();
    _stateManager->setState(SystemState::BOOT);

    // 2. I2C bus
    Wire.begin();

    // 3. OLED
    _oledStatus = new OledStatus();
    _oledStatus->begin();
    _oledStatus->showBoot();
    delay(500);

    // 4. DS3231 RTC
    _ds3231 = new Ds3231Manager();
    _ds3231Present = _ds3231->begin();
    _oledStatus->showDs3231Status(_ds3231Present);
    delay(800);

    // 5. Load config
    _stateManager->setState(SystemState::LOADING_CONFIG);
    _oledStatus->showState(SystemState::LOADING_CONFIG);
    _configManager = new ConfigManager();
    _configManager->begin();
    delay(300);

    // 6. Set system time from DS3231 if present
    if (_ds3231Present) {
        _ds3231->applyToSystemClock();
        Serial.println("[App] System time set from DS3231.");
    }

    // 7. Init relays (all OFF – safe state)
    _relayManager = new RelayManager();
    _relayManager->begin(_configManager->getHardwareConfig());

    // 8. WiFi
    _wifiManager = new WifiManager();
    startWifi();

    // 9. Web server
    _webServer = new WebServerManager();
    startWebServer();

    // 10. NTP time sync (only when WiFi connected)
    _timeSync = new TimeSync();
    if (_wifiManager->isConnected()) {
        _stateManager->setState(SystemState::TIME_SYNC);
        _oledStatus->showState(SystemState::TIME_SYNC);
        _timeSync->begin(_configManager->getDeviceConfig(), _ds3231);
    }

    // 11. Final operational state
    if (_wifiManager->isConnected()) {
        _stateManager->setState(SystemState::RUNNING);
    } else if (_configManager->isWateringConfigValid()) {
        _stateManager->setState(SystemState::RUNNING_OFFLINE);
    } else {
        _stateManager->setState(SystemState::SETUP_REQUIRED);
    }
    _oledStatus->showState(_stateManager->getState());
    Serial.printf("[App] Boot complete. State: %s\n", _stateManager->getStateString());
}

void Application::update() {
    // Scheduled restart
    if (_restartScheduled && millis() >= _restartAt) {
        Serial.println("[App] Executing scheduled restart.");
        ESP.restart();
    }

    // Deferred config apply
    if (_configNeedsApply) {
        _configNeedsApply = false;
        executeApplyLiveConfig();
    }

    _wifiManager->update();

    if (_relayManager) {
        _relayManager->update();
    }

    if (_timeSync) {
        _timeSync->update();
    }

    _webServer->handle(_apModeActive);

    _oledStatus->update(_stateManager->getState(),
                         _wifiManager->getLocalIP(),
                         _wifiManager->getApSSID());
}

bool Application::isWateringLocked() const {
    return !_configManager->isWateringConfigValid();
}

void Application::scheduleRestart(int delayMs) {
    _restartScheduled = true;
    _restartAt        = millis() + delayMs;
    _stateManager->setState(SystemState::RESTARTING);
    Serial.printf("[App] Restart scheduled in %d ms.\n", delayMs);
}

void Application::requestConfigApply() {
    _configNeedsApply = true;
}

// ─── Private ──────────────────────────────────────────────────────────────────

void Application::startWifi() {
    _stateManager->setState(SystemState::WIFI_SCANNING);
    _oledStatus->showState(SystemState::WIFI_SCANNING);

    DeviceConfig& cfg = _configManager->getDeviceConfig();

    bool connected = _wifiManager->begin(cfg);
    _apModeActive  = _wifiManager->isApModeActive();

    if (connected) {
        _stateManager->setState(SystemState::WIFI_CONNECTED);
        _oledStatus->showState(SystemState::WIFI_CONNECTED);
        Serial.printf("[App] WiFi connected, IP: %s\n", _wifiManager->getLocalIP().c_str());
    } else if (_apModeActive) {
        _stateManager->setState(SystemState::WIFI_AP_MODE);
        _oledStatus->showApMode(_wifiManager->getApSSID().c_str(), "192.168.4.1");
    } else {
        Serial.println("[App] WiFi not connected, no AP mode.");
    }
}

void Application::startWebServer() {
    _webServer->begin(this, _apModeActive);
    Serial.println("[App] Web server started.");
}

void Application::executeApplyLiveConfig() {
    Serial.println("[App] Applying live config changes.");
    // Re-apply timezone in TimeSync
    if (_timeSync) {
        _timeSync->setTimezone(_configManager->getDeviceConfig().timezone);
    }
    // Relay re-init with (possibly updated) hardware config
    if (_relayManager) {
        _relayManager->begin(_configManager->getHardwareConfig());
    }
}
