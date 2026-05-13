#include "Application.h"
#include "StateManager.h"
#include "ConfigManager.h"
#include "WifiManager.h"
#include "TimeSync.h"
#include "OledStatus.h"
#include "TftStatus.h"
#include "Ds3231Manager.h"
#include "RelayManager.h"
#include "WebServerManager.h"
#include "WeatherManager.h"
#include "WateringScheduler.h"
#include "WateringRunLog.h"
#include "Version.hpp"
#include <ArduinoOTA.h>
#include <Wire.h>

Application::Application() {}

Application::~Application() {
    delete _scheduler;
    delete _runLog;
    delete _weatherManager;
    delete _stateManager;
    delete _configManager;
    delete _wifiManager;
    delete _timeSync;
    delete _oledStatus;
    delete _tftStatus;
    delete _ds3231;
    delete _relayManager;
    delete _webServer;
}

void Application::begin() {
    Serial.printf("[App] Firmware %s (base=%s, git=%s, build=%s)\n",
                  BewaesserungVersion::kFullVersion,
                  BewaesserungVersion::kBaseVersion,
                  BewaesserungVersion::kGitHash,
                  BewaesserungVersion::kBuildDate);

    // 1. Boot state
    _stateManager = new StateManager();
    _stateManager->setState(SystemState::BOOT);

    // 2. Load config from LittleFS (does not need Wire/I2C)
    _configManager = new ConfigManager();
    _configManager->begin();

    // 3. I2C bus (needed by OLED and DS3231)
    Wire.begin();

    // 4. Display init based on hardware config
    //    displayMode: DISPLAY_OLED (0) / DISPLAY_TFT (1) / DISPLAY_BOTH (2)
    {
        HardwareConfig& hw = _configManager->getHardwareConfig();
        if (hw.displayMode == DISPLAY_OLED || hw.displayMode == DISPLAY_BOTH) {
            _oledStatus = new OledStatus();
            _oledStatus->begin();
        }
        if (hw.displayMode == DISPLAY_TFT || hw.displayMode == DISPLAY_BOTH) {
            _tftStatus = new TftStatus(hw.tftCsPin, hw.tftDcPin, hw.tftRstPin);
            _tftStatus->begin();
        }
    }
    if (_oledStatus) _oledStatus->showBoot();
    if (_tftStatus)  _tftStatus->showBoot();
    delay(500);

    // 5. DS3231 RTC
    _ds3231 = new Ds3231Manager();
    _ds3231Present = _ds3231->begin();
    if (_oledStatus) _oledStatus->showDs3231Status(_ds3231Present);
    if (_tftStatus)  _tftStatus->showDs3231Status(_ds3231Present);
    delay(800);

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
    startOta();

    // 10. NTP time sync (only when WiFi connected)
    _timeSync = new TimeSync();
    if (_wifiManager->isConnected()) {
        _stateManager->setState(SystemState::TIME_SYNC);
        if (_oledStatus) _oledStatus->showState(SystemState::TIME_SYNC);
        if (_tftStatus)  _tftStatus->showState(SystemState::TIME_SYNC);
        _timeSync->begin(_configManager->getDeviceConfig(), _ds3231);
    }

    // 11. Weather manager
    _weatherManager = new WeatherManager();
    _weatherManager->begin(_configManager);

    // 12. Watering scheduler + run log
    _runLog   = new WateringRunLog();
    _scheduler = new WateringScheduler();
    _scheduler->setRunLog(_runLog);
    _scheduler->begin(_configManager, _relayManager, _weatherManager);

    // 13. Final operational state
    if (_wifiManager->isConnected()) {
        _stateManager->setState(SystemState::RUNNING);
    } else if (_configManager->isWateringConfigValid()) {
        _stateManager->setState(SystemState::RUNNING_OFFLINE);
    } else {
        _stateManager->setState(SystemState::SETUP_REQUIRED);
    }
    if (_oledStatus) _oledStatus->showState(_stateManager->getState());
    if (_tftStatus)  _tftStatus->showState(_stateManager->getState());
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

    bool otaNetworkReady = _wifiManager->isConnected() || _wifiManager->isApModeActive();
    if (otaNetworkReady && !_otaNetworkReadyLast) {
        startOta();
    }
    _otaNetworkReadyLast = otaNetworkReady;

    if (_relayManager) {
        _relayManager->update();
    }

    if (_timeSync) {
        _timeSync->update();
    }

    if (_weatherManager) {
        _weatherManager->update();
    }

    if (_scheduler) {
        _scheduler->update();
    }

    if (_otaStarted) {
        ArduinoOTA.handle();
    }

    _webServer->handle(_apModeActive);

    // ── Status displays ───────────────────────────────────────────────────────
    // Collect pump active/enabled states once so both displays share the data.
    bool pumpActive[MAX_RELAY_COUNT]  = {};
    bool pumpEnabled[MAX_RELAY_COUNT] = {};
    int  pumpCount = 0;
    if (_configManager && _relayManager) {
        HardwareConfig& hw = _configManager->getHardwareConfig();
        pumpCount = hw.relayCount;
        for (int i = 0; i < pumpCount; i++) {
            pumpActive[i]  = _relayManager->isActive(i);
            pumpEnabled[i] = hw.pumps[i].enabled;
        }
    }

    if (_oledStatus) {
        _oledStatus->update(_stateManager->getState(),
                            _wifiManager->getLocalIP(),
                            _wifiManager->getApSSID());
    }
    if (_tftStatus) {
        _tftStatus->update(_stateManager->getState(),
                           _wifiManager->getLocalIP(),
                           _wifiManager->getApSSID(),
                           pumpActive, pumpEnabled, pumpCount);
    }
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
    if (_oledStatus) _oledStatus->showState(SystemState::WIFI_SCANNING);
    if (_tftStatus)  _tftStatus->showState(SystemState::WIFI_SCANNING);

    DeviceConfig& cfg = _configManager->getDeviceConfig();

    bool connected = _wifiManager->begin(cfg);
    _apModeActive  = _wifiManager->isApModeActive();

    if (connected) {
        _stateManager->setState(SystemState::WIFI_CONNECTED);
        if (_oledStatus) _oledStatus->showState(SystemState::WIFI_CONNECTED);
        if (_tftStatus)  _tftStatus->showState(SystemState::WIFI_CONNECTED);
        Serial.printf("[App] WiFi connected, IP: %s\n", _wifiManager->getLocalIP().c_str());
    } else if (_apModeActive) {
        _stateManager->setState(SystemState::WIFI_AP_MODE);
        if (_oledStatus) _oledStatus->showApMode(_wifiManager->getApSSID().c_str(), "192.168.4.1");
        if (_tftStatus)  _tftStatus->showApMode(_wifiManager->getApSSID().c_str(), "192.168.4.1");
    } else {
        Serial.println("[App] WiFi not connected, no AP mode.");
    }
}

void Application::startWebServer() {
    _webServer->begin(this, _apModeActive);
    Serial.println("[App] Web server started.");
}

void Application::startOta() {
    if (_otaStarted || !_configManager || !_wifiManager) return;
    if (!_wifiManager->isConnected() && !_wifiManager->isApModeActive()) return;

    DeviceConfig& cfg = _configManager->getDeviceConfig();
    ArduinoOTA.setHostname(cfg.hostname[0] ? cfg.hostname : "Bewaesserung");
    if (cfg.otaPassword[0]) {
        ArduinoOTA.setPassword(cfg.otaPassword);
    }
    ArduinoOTA.onStart([]() {
        Serial.println("[OTA] Update started.");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\n[OTA] Update finished.");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        unsigned int pct = total ? (progress * 100U) / total : 0;
        Serial.printf("[OTA] Progress: %u%%\r", pct);
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[OTA] Error %u\n", (unsigned)error);
    });
    ArduinoOTA.begin();
    _otaStarted = true;
    Serial.printf("[OTA] Ready on %s as '%s'.\n",
                  _wifiManager->isApModeActive() ? "AP" : "WLAN",
                  cfg.hostname[0] ? cfg.hostname : "Bewaesserung");
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
    // Re-init scheduler with updated config
    if (_scheduler) {
        _scheduler->begin(_configManager, _relayManager, _weatherManager);
    }
    // Re-init displays based on (possibly updated) display config
    HardwareConfig& hw = _configManager->getHardwareConfig();
    delete _oledStatus; _oledStatus = nullptr;
    delete _tftStatus;  _tftStatus  = nullptr;
    if (hw.displayMode == DISPLAY_OLED || hw.displayMode == DISPLAY_BOTH) {
        _oledStatus = new OledStatus();
        _oledStatus->begin();
    }
    if (hw.displayMode == DISPLAY_TFT || hw.displayMode == DISPLAY_BOTH) {
        _tftStatus = new TftStatus(hw.tftCsPin, hw.tftDcPin, hw.tftRstPin);
        _tftStatus->begin();
    }
}

// (end of Application.cpp)
