#pragma once

// Forward declarations
class StateManager;
class ConfigManager;
class WifiManager;
class TimeSync;
class OledStatus;
class Ds3231Manager;
class RelayManager;
class WebServerManager;

class Application {
public:
    Application();
    ~Application();
    void begin();
    void update();

    // State accessors used by web handlers
    bool isDs3231Present()   const { return _ds3231Present; }
    bool isWateringLocked()  const;
    bool isApModeActive()    const { return _apModeActive; }

    // Scheduling
    void scheduleRestart(int delayMs = 1500);
    void requestConfigApply();

    // Accessors for web handlers
    ConfigManager*    getConfigManager()  { return _configManager; }
    WifiManager*      getWifiManager()    { return _wifiManager; }
    TimeSync*         getTimeSync()       { return _timeSync; }
    Ds3231Manager*    getDs3231()         { return _ds3231; }
    RelayManager*     getRelayManager()   { return _relayManager; }
    StateManager*     getStateManager()   { return _stateManager; }

private:
    void startWifi();
    void startWebServer();
    void executeApplyLiveConfig();

    StateManager*     _stateManager   = nullptr;
    ConfigManager*    _configManager  = nullptr;
    WifiManager*      _wifiManager    = nullptr;
    TimeSync*         _timeSync       = nullptr;
    OledStatus*       _oledStatus     = nullptr;
    Ds3231Manager*    _ds3231         = nullptr;
    RelayManager*     _relayManager   = nullptr;
    WebServerManager* _webServer      = nullptr;

    bool          _configNeedsApply   = false;
    bool          _restartScheduled   = false;
    unsigned long _restartAt          = 0;
    bool          _apModeActive       = false;
    bool          _ds3231Present      = false;
};
