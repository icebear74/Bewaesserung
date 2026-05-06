#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#define MAX_RELAY_COUNT 8

// Output types for pump entries
#define OUTPUT_TYPE_GPIO    0   // Direct GPIO pin
#define OUTPUT_TYPE_PCF8574 1   // PCF8574 / PCF8575 I2C GPIO expander

// Convenience alias kept for backward compatibility
#define OUTPUT_TYPE_I2C OUTPUT_TYPE_PCF8574

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

// Per-pump configuration entry
struct PumpEntry {
    bool    enabled         = false;
    char    name[32]        = "";
    uint8_t outputType      = OUTPUT_TYPE_GPIO;
    int     pin             = -1;           // GPIO pin (OUTPUT_TYPE_GPIO only)
    uint8_t i2cAddress      = 0x20;        // I2C address (OUTPUT_TYPE_PCF8574 only)
    uint8_t i2cChannel      = 0;           // Pin/channel on expander (0-7 PCF8574, 0-15 PCF8575)
    bool    invertLogic     = false;        // active-low per pump
    int     maxRuntimeSec   = 300;          // max runtime / test timeout
    char    notes[64]       = "";
};

struct HardwareConfig {
    int       relayCount            = 0;
    PumpEntry pumps[MAX_RELAY_COUNT];
    bool      relayInverted         = false;  // legacy global invert flag (kept for backward compat)
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
