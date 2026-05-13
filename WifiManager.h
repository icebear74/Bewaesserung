#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "ConfigManager.h"

#define WPS_TIMEOUT_MS           60000UL
#define CONNECT_TIMEOUT_MS       10000UL
#define RECONNECT_INTERVAL_MS    30000UL
#define HEALTH_CHECK_INTERVAL_MS 60000UL

class WifiManager {
public:
    WifiManager();
    bool begin(DeviceConfig& config);
    void update();

    bool   isConnected()           const { return _connected; }
    String getLocalIP()            const { return _localIP; }
    String getApSSID()             const { return _apSSID; }
    bool   isApModeActive()        const { return _apModeActive; }
    String getSSID()               const { return _ssid; }
    int    getRSSI()               const { return _connected ? WiFi.RSSI() : -100; }
    time_t getConnectedSinceEpoch() const { return _connectedSinceEpoch; }

    void reconnect(DeviceConfig& config);

private:
    bool connectMultiAP(DeviceConfig& config);
    bool tryWPS();
    void startApMode();
    bool healthCheck();

    bool          _connected              = false;
    bool          _apModeActive           = false;
    String        _apSSID;
    String        _localIP;
    String        _ssid;
    time_t        _connectedSinceEpoch    = 0;
    unsigned long _lastReconnectAttempt   = 0;
    unsigned long _lastHealthCheckMs      = 0;
    DeviceConfig* _config                 = nullptr;
};
