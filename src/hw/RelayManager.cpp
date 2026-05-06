#include "RelayManager.h"

RelayManager::RelayManager() {}

void RelayManager::begin(HardwareConfig& config) {
    _config = config;

    // Configure all relay GPIO pins and set safe (off) state
    for (int i = 0; i < _config.relayCount; i++) {
        int pin = _config.relayPins[i];
        if (pin < 0) continue;
        pinMode(pin, OUTPUT);
        // Write safe state immediately before setting direction is established
        writeRelay(i, false);
    }
    allOff();  // Ensure all are off
    Serial.printf("[Relay] Initialized %d relay(s), inverted=%s\n",
                  _config.relayCount, _config.relayInverted ? "yes" : "no");
}

bool RelayManager::activateRelay(int index, bool armed) {
    if (!armed) {
        Serial.printf("[Relay] Activate relay %d blocked – not armed (safety lock).\n", index);
        return false;
    }
    if (index < 0 || index >= _config.relayCount) {
        Serial.printf("[Relay] activateRelay: index %d out of range.\n", index);
        return false;
    }
    if (_config.relayPins[index] < 0) {
        Serial.printf("[Relay] activateRelay: relay %d has no pin assigned.\n", index);
        return false;
    }
    writeRelay(index, true);
    _relayState[index] = true;
    Serial.printf("[Relay] Relay %d (pin %d) ON\n", index, _config.relayPins[index]);
    return true;
}

bool RelayManager::deactivateRelay(int index) {
    if (index < 0 || index >= _config.relayCount) return false;
    writeRelay(index, false);
    _relayState[index] = false;
    Serial.printf("[Relay] Relay %d (pin %d) OFF\n", index, _config.relayPins[index]);
    return true;
}

void RelayManager::allOff() {
    for (int i = 0; i < MAX_RELAY_COUNT; i++) {
        if (i < _config.relayCount && _config.relayPins[i] >= 0) {
            writeRelay(i, false);
        }
        _relayState[i] = false;
    }
    Serial.println("[Relay] All relays OFF.");
}

bool RelayManager::isActive(int index) const {
    if (index < 0 || index >= MAX_RELAY_COUNT) return false;
    return _relayState[index];
}

void RelayManager::writeRelay(int index, bool on) {
    int pin = _config.relayPins[index];
    if (pin < 0) return;
    // XOR with inverted flag: active-low modules need LOW to turn on
    bool level = on ^ _config.relayInverted;
    digitalWrite(pin, level ? HIGH : LOW);
}
