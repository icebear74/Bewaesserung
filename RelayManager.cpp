#include "RelayManager.h"

RelayManager::RelayManager() {
    memset(_pcfOk, false, sizeof(_pcfOk));
}

// ─── Private helpers ──────────────────────────────────────────────────────────

bool RelayManager::isPumpValid(const PumpEntry& p) const {
    if (!p.enabled) return false;
    if (p.outputType == OUTPUT_TYPE_GPIO) return p.pin >= 0;
    if (p.outputType == OUTPUT_TYPE_PCF8574) {
        if (p.expanderIndex >= (uint8_t)_config.expanderCount) return false;
        const ExpanderEntry& e = _config.expanders[p.expanderIndex];
        if (!e.enabled || !_pcfOk[p.expanderIndex]) return false;
        uint8_t maxChannel = (e.chipType == EXPANDER_TYPE_PCF8575) ? 15 : 7;
        return p.i2cChannel <= maxChannel;
    }
    return false;
}

// ─── Public API ───────────────────────────────────────────────────────────────

void RelayManager::begin(HardwareConfig& config) {
    _config = config;
    _config.relayCount    = constrain(_config.relayCount, 0, MAX_RELAY_COUNT);
    _config.expanderCount = constrain(_config.expanderCount, 0, MAX_EXPANDER_COUNT);

    // Cancel any pending test timeouts on reload
    for (int i = 0; i < MAX_RELAY_COUNT; i++) {
        _testOffAt[i] = 0;
    }

    // ── GPIO pins ─────────────────────────────────────────────────────────────
    // Pre-set the output register to the safe (off) level BEFORE enabling the
    // pin as OUTPUT.  On ESP32, digitalWrite() before pinMode(OUTPUT) pre-charges
    // the output latch, so active-low relays (invertLogic=true) never glitch ON
    // during initialisation.
    for (int i = 0; i < _config.relayCount; i++) {
        const PumpEntry& p = _config.pumps[i];
        if (!p.enabled || p.outputType != OUTPUT_TYPE_GPIO || p.pin < 0) continue;
        bool offLevel = p.invertLogic || _config.relayInverted;
        digitalWrite(p.pin, offLevel ? HIGH : LOW);
        pinMode(p.pin, OUTPUT);
    }

    // ── PCF8574 / PCF8575 expanders ───────────────────────────────────────────
    // Each expander is indexed directly by its position in _config.expanders[].
    memset(_pcfOk, false, sizeof(_pcfOk));
    for (int d = 0; d < _config.expanderCount; d++) {
        const ExpanderEntry& e = _config.expanders[d];
        if (!e.enabled) continue;
        _pcfOk[d] = _pcfDevices[d].begin(e.i2cAddress);
        if (_pcfOk[d]) {
            const char* typeName = (e.chipType == EXPANDER_TYPE_PCF8575) ? "PCF8575" : "PCF8574";
            Serial.printf("[Relay] %s \"%s\" (0x%02X) initialized.\n",
                          typeName, e.name, e.i2cAddress);
            // Immediately write the safe (off) level to every pump on this expander.
            // This minimises the window during which PCF8574 pins sit at their
            // power-on HIGH state and could briefly activate a non-inverted relay.
            for (int i = 0; i < _config.relayCount; i++) {
                const PumpEntry& p = _config.pumps[i];
                if (p.enabled && p.outputType == OUTPUT_TYPE_PCF8574 &&
                    p.expanderIndex == (uint8_t)d) {
                    writeRelay(i, false);
                }
            }
        } else {
            Serial.printf("[Relay] \"%s\" (0x%02X) NOT found on I2C bus!\n",
                          e.name, e.i2cAddress);
        }
    }

    allOff();  // Final safe-state pass for all outputs
    Serial.printf("[Relay] Initialized %d pump(s), %d expander(s).\n",
                  _config.relayCount, _config.expanderCount);
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
        const char* expName = _config.expanders[p.expanderIndex].name;
        Serial.printf("[Relay] Relay %d (\"%s\" ch%d) ON\n", index, expName, p.i2cChannel);
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
        const char* expName = _config.expanders[p.expanderIndex].name;
        Serial.printf("[Relay] Relay %d (\"%s\" ch%d) OFF\n", index, expName, p.i2cChannel);
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
        Serial.printf("[Relay] TEST pump %d (GPIO %d) ON – auto-off in %ds\n",
                      index, p.pin, timeout);
    } else {
        const char* expName = _config.expanders[p.expanderIndex].name;
        Serial.printf("[Relay] TEST pump %d (\"%s\" ch%d) ON – auto-off in %ds\n",
                      index, expName, p.i2cChannel, timeout);
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
        const char* expName = _config.expanders[p.expanderIndex].name;
        Serial.printf("[Relay] TEST pump %d (\"%s\" ch%d) OFF\n",
                      index, expName, p.i2cChannel);
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
        uint8_t di = p.expanderIndex;
        if (di >= MAX_EXPANDER_COUNT || !_pcfOk[di]) {
            Serial.printf("[Relay] writeRelay: expander %d not available.\n", di);
            return;
        }
        uint8_t maxChannel = (_config.expanders[di].chipType == EXPANDER_TYPE_PCF8575) ? 15 : 7;
        if (p.i2cChannel > maxChannel) {
            Serial.printf("[Relay] writeRelay: channel %d out of range for expander %d.\n",
                          p.i2cChannel, di);
            return;
        }
        _pcfDevices[di].digitalWrite(p.i2cChannel, level);
    }
}
