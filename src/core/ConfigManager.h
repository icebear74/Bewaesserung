#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#define MAX_RELAY_COUNT 8

struct DeviceConfig {
    char hostname[32]    = "Bewaesserung";
    char ssid[64]        = "";
    char password[64]    = "";
    char timezone[64]    = "CET-1CEST,M3.5.0,M10.5.0/3";
    char ntpServer[64]   = "pool.ntp.org";
    float latitude       = 48.1351;
    float longitude      = 11.5820;
    char locationName[64]= "";
};

struct HardwareConfig {
    int relayCount                   = 0;
    int relayPins[MAX_RELAY_COUNT]   = {-1,-1,-1,-1,-1,-1,-1,-1};
    bool relayInverted               = false;  // true = active low
};

struct WateringEntry {
    int relay;         // relay index 0-based
    int hour;          // 0-23
    int minute;        // 0-59
    int durationSec;   // duration in seconds
    bool active;       // enabled/disabled
    uint8_t days;      // bitmask: bit0=Mon,...bit6=Sun
};

#define MAX_WATERING_ENTRIES 32

struct WateringConfig {
    int count = 0;
    WateringEntry entries[MAX_WATERING_ENTRIES];
};

class ConfigManager {
public:
    ConfigManager();
    void begin();

    bool loadDeviceConfig();
    bool saveDeviceConfig();
    DeviceConfig& getDeviceConfig() { return _deviceConfig; }

    bool loadHardwareConfig();
    bool saveHardwareConfig();
    HardwareConfig& getHardwareConfig() { return _hardwareConfig; }

    bool loadWateringConfig();
    bool saveWateringConfig();
    WateringConfig& getWateringConfig() { return _wateringConfig; }

    bool isWateringConfigValid() const;

    bool resetAll();

private:
    DeviceConfig   _deviceConfig;
    HardwareConfig _hardwareConfig;
    WateringConfig _wateringConfig;
};
