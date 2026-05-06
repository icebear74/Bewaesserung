#include "RelayManager.h"

RelayManager::RelayManager() {}

void RelayManager::begin(HardwareConfig& config) {
    _config = config;

    // Cancel any pending test timeouts on reload
    for (int i = 0; i < MAX_RELAY_COUNT; i++) {
        _testOffAt[i] = 0;
    }

    // Configure all enabled pump GPIO pins and set safe (off) state
    for (int i = 0; i < _config.relayCount; i++) {
        const PumpEntry& p = _config.pumps[i];
        if (!p.enabled || p.pin < 0 || p.outputType != OUTPUT_TYPE_GPIO) continue;
        pinMode(p.pin, OUTPUT);
        writeRelay(i, false);  // Safe (off) state immediately
    }
    allOff();  // Ensure all are off
    Serial.printf("[Relay] Initialized %d pump(s).\n", _config.relayCount);
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
    const PumpEntry& p = _config.pumps[index];
    if (!p.enabled) {
        Serial.printf("[Relay] activateRelay: pump %d is disabled.\n", index);
        return false;
    }
    if (p.pin < 0) {
        Serial.printf("[Relay] activateRelay: pump %d has no pin assigned.\n", index);
        return false;
    }
    if (p.outputType != OUTPUT_TYPE_GPIO) {
        Serial.printf("[Relay] activateRelay: pump %d output type %d not yet supported.\n", index, p.outputType);
        return false;
    }
    writeRelay(index, true);
    _relayState[index] = true;
    Serial.printf("[Relay] Relay %d (pin %d) ON\n", index, p.pin);
    return true;
}

bool RelayManager::deactivateRelay(int index) {
    if (index < 0 || index >= _config.relayCount) return false;
    writeRelay(index, false);
    _relayState[index] = false;
    _testOffAt[index]  = 0;
    Serial.printf("[Relay] Relay %d (pin %d) OFF\n", index, _config.pumps[index].pin);
    return true;
}

void RelayManager::allOff() {
    for (int i = 0; i < MAX_RELAY_COUNT; i++) {
        if (i < _config.relayCount && _config.pumps[i].pin >= 0) {
            writeRelay(i, false);
        }
        _relayState[i] = false;
        _testOffAt[i]  = 0;
    }
    Serial.println("[Relay] All relays OFF.");
}

bool RelayManager::isActive(int index) const {
    if (index < 0 || index >= MAX_RELAY_COUNT) return false;
    return _relayState[index];
}

bool RelayManager::testActivateRelay(int index) {
    if (index < 0 || index >= _config.relayCount) {
        Serial.printf("[Relay] testActivateRelay: index %d out of range.\n", index);
        return false;
    }
    const PumpEntry& p = _config.pumps[index];
    if (!p.enabled) {
        Serial.printf("[Relay] testActivateRelay: pump %d is disabled.\n", index);
        return false;
    }
    if (p.pin < 0) {
        Serial.printf("[Relay] testActivateRelay: pump %d has no pin assigned.\n", index);
        return false;
    }
    if (p.outputType != OUTPUT_TYPE_GPIO) {
        Serial.printf("[Relay] testActivateRelay: pump %d output type not yet supported.\n", index);
        return false;
    }
    writeRelay(index, true);
    _relayState[index] = true;
    int timeout = (p.maxRuntimeSec > 0) ? p.maxRuntimeSec : 30;
    _testOffAt[index] = millis() + ((unsigned long)timeout * 1000UL);
    Serial.printf("[Relay] TEST pump %d (pin %d) ON – auto-off in %ds\n", index, p.pin, timeout);
    return true;
}

bool RelayManager::testDeactivateRelay(int index) {
    if (index < 0 || index >= _config.relayCount) {
        Serial.printf("[Relay] testDeactivateRelay: index %d out of range.\n", index);
        return false;
    }
    const PumpEntry& p = _config.pumps[index];
    if (p.pin < 0) return false;
    writeRelay(index, false);
    _relayState[index] = false;
    _testOffAt[index]  = 0;
    Serial.printf("[Relay] TEST pump %d (pin %d) OFF\n", index, p.pin);
    return true;
}

void RelayManager::update() {
    unsigned long now = millis();
    for (int i = 0; i < _config.relayCount; i++) {
        // Use unsigned subtraction for correct wraparound behaviour (~49-day cycle)
        if (_testOffAt[i] != 0 && (now - _testOffAt[i]) < 0x80000000UL) {
            writeRelay(i, false);
            _relayState[i] = false;
            _testOffAt[i]  = 0;
            Serial.printf("[Relay] TEST pump %d auto-off (timeout reached)\n", i);
        }
    }
}

void RelayManager::writeRelay(int index, bool on) {
    const PumpEntry& p = _config.pumps[index];
    if (p.pin < 0) return;
    // Per-pump invertLogic OR legacy global relayInverted flag
    bool level = on ^ (p.invertLogic || _config.relayInverted);
    digitalWrite(p.pin, level ? HIGH : LOW);
}
