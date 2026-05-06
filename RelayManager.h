#pragma once
#include <Arduino.h>
#include <Adafruit_PCF8574.h>
#include "ConfigManager.h"

// Maximum number of distinct PCF8574 devices supported (one per unique I2C address)
#define MAX_PCF8574_DEVICES 4

class RelayManager {
public:
    RelayManager();
    void begin(HardwareConfig& config);

    // Scheduled watering: only activates if armed (safety lock)
    bool activateRelay(int index, bool armed);
    bool deactivateRelay(int index);
    void allOff();
    bool isActive(int index) const;
    int  getRelayCount() const { return _config.relayCount; }

    // Test control: no armed check, bounded by maxRuntimeSec timeout
    bool testActivateRelay(int index);
    bool testDeactivateRelay(int index);

    // Call from main loop to handle test timeouts
    void update();

private:
    void writeRelay(int index, bool on);
    // Returns the index in _pcfDevices[] for a given I2C address, or -1 if not found
    int  findPcf(uint8_t address) const;
    // Returns index (existing or newly added) for a given address, or -1 if pool full
    int  findOrAddPcf(uint8_t address);
    // Returns true when a pump entry has a valid output configuration
    bool isPumpValid(const PumpEntry& p) const;

    HardwareConfig   _config;
    bool             _relayState[MAX_RELAY_COUNT]  = {false};
    unsigned long    _testOffAt[MAX_RELAY_COUNT]   = {0};

    Adafruit_PCF8574 _pcfDevices[MAX_PCF8574_DEVICES];
    uint8_t          _pcfAddresses[MAX_PCF8574_DEVICES];
    bool             _pcfOk[MAX_PCF8574_DEVICES];
    int              _pcfCount = 0;
};
