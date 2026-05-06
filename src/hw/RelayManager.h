#pragma once
#include <Arduino.h>
#include "core/ConfigManager.h"

class RelayManager {
public:
    RelayManager();
    void begin(HardwareConfig& config);

    // Only activates if armed (safety lock)
    bool activateRelay(int index, bool armed);
    bool deactivateRelay(int index);
    void allOff();
    bool isActive(int index) const;
    int  getRelayCount() const { return _config.relayCount; }

private:
    void writeRelay(int index, bool on);

    HardwareConfig _config;
    bool           _relayState[MAX_RELAY_COUNT] = {false};
};
