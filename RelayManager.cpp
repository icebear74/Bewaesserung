#include "RelayManager.h"

RelayManager::RelayManager() {
    memset(_pcfAddresses, 0, sizeof(_pcfAddresses));
    memset(_pcfOk, false, sizeof(_pcfOk));
}

// ─── Private helpers ──────────────────────────────────────────────────────────

int RelayManager::findPcf(uint8_t address) const {
    for (int d = 0; d < _pcfCount; d++) {
        if (_pcfAddresses[d] == address) return d;
    }
    return -1;
}

int RelayManager::findOrAddPcf(uint8_t address) {
    int idx = findPcf(address);
    if (idx >= 0) return idx;
    if (_pcfCount >= MAX_PCF8574_DEVICES) {
        Serial.printf("[Relay] findOrAddPcf: device pool full (max %d devices).\n", MAX_PCF8574_DEVICES);
        return -1;
    }
    _pcfAddresses[_pcfCount] = address;
    return _pcfCount++;
}

bool RelayManager::isPumpValid(const PumpEntry& p) const {
    if (!p.enabled) return false;
    if (p.outputType == OUTPUT_TYPE_GPIO)    return p.pin >= 0;
    if (p.outputType == OUTPUT_TYPE_PCF8574) return true;
    return false;
}

// ─── Public API ───────────────────────────────────────────────────────────────

void RelayManager::begin(HardwareConfig& config) {
    _config = config;

    // Cancel any pending test timeouts on reload
    for (int i = 0; i < MAX_RELAY_COUNT; i++) {
        _testOffAt[i] = 0;
    }

    // ── GPIO pins: configure direction and set safe (off) state ──────────────
    for (int i = 0; i < _config.relayCount; i++) {
        const PumpEntry& p = _config.pumps[i];
        if (!p.enabled || p.outputType != OUTPUT_TYPE_GPIO || p.pin < 0) continue;
        pinMode(p.pin, OUTPUT);
        writeRelay(i, false);
    }

    // ── PCF8574 devices: collect unique addresses, then initialize ────────────
    _pcfCount = 0;
    memset(_pcfAddresses, 0, sizeof(_pcfAddresses));
    memset(_pcfOk, false, sizeof(_pcfOk));

    for (int i = 0; i < _config.relayCount; i++) {
        const PumpEntry& p = _config.pumps[i];
        if (!p.enabled || p.outputType != OUTPUT_TYPE_PCF8574) continue;
        findOrAddPcf(p.i2cAddress);
    }
    for (int d = 0; d < _pcfCount; d++) {
        _pcfOk[d] = _pcfDevices[d].begin(_pcfAddresses[d]);
        if (_pcfOk[d]) {
            Serial.printf("[Relay] PCF8574 at 0x%02X initialized.\n", _pcfAddresses[d]);
        } else {
            Serial.printf("[Relay] PCF8574 at 0x%02X NOT found on I2C bus!\n", _pcfAddresses[d]);
        }
    }

    allOff();  // Ensure all outputs are in safe (off) state
    Serial.printf("[Relay] Initialized %d pump(s), %d PCF8574 device(s).\n",
                  _config.relayCount, _pcfCount);
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
    if (!isPumpValid(p)) {
        Serial.printf("[Relay] activateRelay: pump %d has invalid configuration.\n", index);
        return false;
    }
    writeRelay(index, true);
    _relayState[index] = true;
    if (p.outputType == OUTPUT_TYPE_GPIO) {
        Serial.printf("[Relay] Relay %d (GPIO %d) ON\n", index, p.pin);
    } else {
        Serial.printf("[Relay] Relay %d (PCF8574 0x%02X ch%d) ON\n", index, p.i2cAddress, p.i2cChannel);
    }
    return true;
}

bool RelayManager::deactivateRelay(int index) {
    if (index < 0 || index >= _config.relayCount) return false;
    writeRelay(index, false);
    _relayState[index] = false;
    _testOffAt[index]  = 0;
    const PumpEntry& p = _config.pumps[index];
    if (p.outputType == OUTPUT_TYPE_GPIO) {
        Serial.printf("[Relay] Relay %d (GPIO %d) OFF\n", index, p.pin);
    } else {
        Serial.printf("[Relay] Relay %d (PCF8574 0x%02X ch%d) OFF\n", index, p.i2cAddress, p.i2cChannel);
    }
    return true;
}

void RelayManager::allOff() {
    for (int i = 0; i < MAX_RELAY_COUNT; i++) {
        if (i < _config.relayCount && isPumpValid(_config.pumps[i])) {
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
    if (!isPumpValid(p)) {
        Serial.printf("[Relay] testActivateRelay: pump %d has invalid configuration.\n", index);
        return false;
    }
    writeRelay(index, true);
    _relayState[index] = true;
    int timeout = (p.maxRuntimeSec > 0) ? p.maxRuntimeSec : 30;
    _testOffAt[index] = millis() + ((unsigned long)timeout * 1000UL);
    if (p.outputType == OUTPUT_TYPE_GPIO) {
        Serial.printf("[Relay] TEST pump %d (GPIO %d) ON – auto-off in %ds\n", index, p.pin, timeout);
    } else {
        Serial.printf("[Relay] TEST pump %d (PCF8574 0x%02X ch%d) ON – auto-off in %ds\n",
                      index, p.i2cAddress, p.i2cChannel, timeout);
    }
    return true;
}

bool RelayManager::testDeactivateRelay(int index) {
    if (index < 0 || index >= _config.relayCount) {
        Serial.printf("[Relay] testDeactivateRelay: index %d out of range.\n", index);
        return false;
    }
    const PumpEntry& p = _config.pumps[index];
    if (!isPumpValid(p)) return false;
    writeRelay(index, false);
    _relayState[index] = false;
    _testOffAt[index]  = 0;
    if (p.outputType == OUTPUT_TYPE_GPIO) {
        Serial.printf("[Relay] TEST pump %d (GPIO %d) OFF\n", index, p.pin);
    } else {
        Serial.printf("[Relay] TEST pump %d (PCF8574 0x%02X ch%d) OFF\n", index, p.i2cAddress, p.i2cChannel);
    }
    return true;
}

void RelayManager::update() {
    unsigned long now = millis();
    // MILLIS_HALF_RANGE: half of unsigned long range; used for wraparound-safe
    // comparison after ~49 days when millis() rolls over.
    static const unsigned long MILLIS_HALF_RANGE = 0x80000000UL;
    for (int i = 0; i < _config.relayCount; i++) {
        // Standard Arduino millis() wraparound-safe "has deadline passed?" pattern:
        // _testOffAt[i] is set to millis() + timeout, i.e. a future timestamp.
        // Case A – not yet reached (now < _testOffAt): unsigned subtraction
        //          underflows to a huge value (>= MILLIS_HALF_RANGE) → FALSE, no shutoff.
        // Case B – deadline reached  (now >= _testOffAt): result is small
        //          (0 .. elapsed ms) < MILLIS_HALF_RANGE → TRUE, shutoff triggered.
        if (_testOffAt[i] != 0 && (now - _testOffAt[i]) < MILLIS_HALF_RANGE) {
            writeRelay(i, false);
            _relayState[i] = false;
            _testOffAt[i]  = 0;
            Serial.printf("[Relay] TEST pump %d auto-off (timeout reached)\n", i);
        }
    }
}

void RelayManager::writeRelay(int index, bool on) {
    const PumpEntry& p = _config.pumps[index];
    // Per-pump invertLogic OR legacy global relayInverted flag
    bool level = on ^ (p.invertLogic || _config.relayInverted);

    if (p.outputType == OUTPUT_TYPE_GPIO) {
        if (p.pin < 0) return;
        digitalWrite(p.pin, level ? HIGH : LOW);
    } else if (p.outputType == OUTPUT_TYPE_PCF8574) {
        int devIdx = findPcf(p.i2cAddress);
        if (devIdx < 0 || !_pcfOk[devIdx]) {
            Serial.printf("[Relay] writeRelay: PCF8574 at 0x%02X not available.\n", p.i2cAddress);
            return;
        }
        _pcfDevices[devIdx].digitalWrite(p.i2cChannel, level);
    }
}
