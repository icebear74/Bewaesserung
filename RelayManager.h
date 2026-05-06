#pragma once
#include <Arduino.h>
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

    HardwareConfig _config;
    bool           _relayState[MAX_RELAY_COUNT]  = {false};
    unsigned long  _testOffAt[MAX_RELAY_COUNT]   = {0};
};
