#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <time.h>

#define MAX_RELAY_COUNT    8
#define MAX_EXPANDER_COUNT 4

// Output types for pump entries
#define OUTPUT_TYPE_GPIO    0   // Direct GPIO pin
#define OUTPUT_TYPE_PCF8574 1   // PCF8574 / PCF8575 I2C GPIO expander

// I2C expander chip types
#define EXPANDER_TYPE_PCF8574 0   // PCF8574: 8 ports  (addresses 0x20–0x27)
#define EXPANDER_TYPE_PCF8575 1   // PCF8575: 16 ports (addresses 0x20–0x27)

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

// Optional hardware: one I2C GPIO-expander chip entry
struct ExpanderEntry {
    bool    enabled    = false;
    char    name[32]   = "";
    uint8_t chipType   = EXPANDER_TYPE_PCF8574;  // 0 = PCF8574 (8 p), 1 = PCF8575 (16 p)
    uint8_t i2cAddress = 0x20;
};

// Per-pump configuration entry
struct PumpEntry {
    bool    enabled        = false;
    char    name[32]       = "";
    uint8_t outputType     = OUTPUT_TYPE_GPIO;
    int     pin            = -1;   // GPIO pin number (OUTPUT_TYPE_GPIO only)
    uint8_t expanderIndex  = 0;    // Index into HardwareConfig.expanders[] (OUTPUT_TYPE_PCF8574 only)
    uint8_t i2cChannel     = 0;    // Channel on expander (0-7 PCF8574, 0-15 PCF8575)
    bool    invertLogic    = false; // active-low per pump
    int     maxRuntimeSec  = 300;   // max runtime / test timeout
    char    notes[64]      = "";
};

struct HardwareConfig {
    int           relayCount    = 0;
    PumpEntry     pumps[MAX_RELAY_COUNT];
    int           expanderCount = 0;
    ExpanderEntry expanders[MAX_EXPANDER_COUNT];
    bool          relayInverted = false;  // legacy global invert flag (kept for backward compat)
};

// ─── Watering Slot / Trigger model ───────────────────────────────────────────

// Trigger types
#define TRIGGER_FIXED_TIME  0   // Fixed clock time (hour:minute)
#define TRIGGER_SUNRISE     1   // Sunrise (from weather data; fallback = fixedHour:fixedMinute)
#define TRIGGER_SUNSET      2   // Sunset  (from weather data; fallback = fixedHour:fixedMinute)
#define TRIGGER_MIDDAY      3   // Midpoint between sunrise and sunset
#define TRIGGER_OFFSET      4   // Offset (+/- minutes) relative to offsetBase

// Base reference for TRIGGER_OFFSET
#define OFFSET_BASE_SUNRISE 0
#define OFFSET_BASE_SUNSET  1
#define OFFSET_BASE_MIDDAY  2

#define MAX_SLOTS           16
#define MAX_SLOT_ASSIGNMENTS 32

// Repeat mode
#define REPEAT_WEEKDAYS      0   // Weekly by weekday bitmask (days)
#define REPEAT_INTERVAL_DAYS 1   // Every N days from intervalAnchorDay

struct WateringSlot {
    char    name[32]          = "";
    bool    enabled           = true;
    uint8_t triggerType       = TRIGGER_FIXED_TIME;
    uint8_t fixedHour         = 6;     // 0–23; also fallback for astronomical triggers
    uint8_t fixedMinute       = 0;     // 0–59
    int16_t offsetMinutes     = 0;     // signed offset in minutes (for TRIGGER_OFFSET)
    uint8_t offsetBase        = OFFSET_BASE_SUNRISE; // OFFSET_BASE_*
    uint8_t repeatMode        = REPEAT_WEEKDAYS;
    uint8_t days              = 0x7F;  // bitmask bit0=Mon … bit6=Sun (default all days)
    uint8_t intervalDays      = 1;     // every N days (REPEAT_INTERVAL_DAYS)
    uint16_t intervalAnchorDay = 0;    // epoch-day anchor for interval mode (local)

    // Legacy slot-level weather policy (kept for backward compatibility).
    // New configs should use assignment-level weather policy.
    float   skipIfRainMm      = 0.0f;  // skip if daily expected precipitation >= x mm
    float   skipIfRainPct     = 0.0f;  // skip if precipitation probability >= x %
    float   runOnlyAboveTemp  = -99.0f;// skip if current temperature < x °C

    float   reduceIfRainMm    = 0.0f;  // reduce duration if daily precip >= x mm
    uint8_t reducePct         = 50;    // percentage to reduce (1–99)
};

struct WeatherPolicy {
    float   skipIfRainMm     = 0.0f;   // 0 = disabled
    float   skipIfRainPct    = 0.0f;   // 0 = disabled
    float   runOnlyAboveTemp = -99.0f; // -99 = disabled
    float   reduceIfRainMm   = 0.0f;   // 0 = disabled
    uint8_t reducePct        = 50;     // 1..99
};

// Assignment: one pump runs for a given duration when a slot fires
struct SlotPumpAssignment {
    uint8_t slotIndex   = 0;    // index into SlotConfig.slots[]
    uint8_t pumpIndex   = 0;    // index into HardwareConfig.pumps[]
    int     durationSec = 60;
    bool    useOwnWeatherPolicy = false; // false => use legacy slot weather for migration
    WeatherPolicy weather;
};

struct SlotConfig {
    int slotCount   = 0;
    WateringSlot slots[MAX_SLOTS];
    int assignCount = 0;
    SlotPumpAssignment assignments[MAX_SLOT_ASSIGNMENTS];
};

// ─── Weather data cache ───────────────────────────────────────────────────────

struct WeatherData {
    float   temperature    = 0.0f;   // °C current
    float   feelsLike      = 0.0f;   // °C apparent temperature
    float   humidity       = 0.0f;   // % relative humidity
    float   precipProb     = 0.0f;   // % precipitation probability (current hour)
    float   precipMm       = 0.0f;   // mm precipitation (current hour)
    float   rain           = 0.0f;   // mm rain
    float   snow           = 0.0f;   // mm snowfall
    float   windSpeed      = 0.0f;   // km/h
    float   windDir        = 0.0f;   // degrees
    float   tempMax        = 0.0f;   // today's maximum temperature
    float   tempMin        = 0.0f;   // today's minimum temperature
    float   dailyPrecipMm  = 0.0f;   // today's total expected precipitation (mm)
    float   dailyPrecipPct = 0.0f;   // today's max precipitation probability (%)
    time_t  sunrise        = 0;      // today's sunrise (local epoch)
    time_t  sunset         = 0;      // today's sunset  (local epoch)
    uint8_t hourlyCount    = 0;      // number of valid forecast samples (max 24)
    time_t  hourlyTime[24] = {0};
    float   hourlyTemp[24] = {0};
    float   hourlyPrecipMm[24] = {0};
    float   hourlyPrecipPct[24] = {0};
    time_t  lastUpdate     = 0;      // when data was last successfully fetched
    bool    available      = false;  // true if data has been fetched at least once
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

    bool loadSlotConfig();
    bool saveSlotConfig();
    SlotConfig& getSlotConfig() { return _slotConfig; }

    bool isWateringConfigValid() const;

    bool resetAll();

private:
    DeviceConfig   _deviceConfig;
    HardwareConfig _hardwareConfig;
    SlotConfig     _slotConfig;
};
