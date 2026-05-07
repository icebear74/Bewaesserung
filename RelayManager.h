#pragma once
#include <Arduino.h>
#include <Adafruit_PCF8574.h>
#include "ConfigManager.h"

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
    // Returns true when a pump entry has a valid output configuration
    bool isPumpValid(const PumpEntry& p) const;

    HardwareConfig   _config;
    bool             _relayState[MAX_RELAY_COUNT]  = {false};
    unsigned long    _testOffAt[MAX_RELAY_COUNT]   = {0};

    // One PCF device per expander entry; indexed directly by ExpanderEntry index
    Adafruit_PCF8574 _pcfDevices[MAX_EXPANDER_COUNT];
    bool             _pcfOk[MAX_EXPANDER_COUNT];
};
