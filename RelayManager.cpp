#include "RelayManager.h"
#include <new>
#include <string.h>
#include <time.h>

static const int WATCHDOG_SHUTDOWN_MAX_RETRIES = 10;
static const int WATCHDOG_SHUTDOWN_RETRY_DELAY_MS = 20;

static bool expanderHasAssignedPump(const HardwareConfig& config, int expanderIndex) {
    if (expanderIndex < 0 || expanderIndex >= config.expanderCount) return false;

    for (int i = 0; i < config.relayCount; i++) {
        const PumpEntry& pump = config.pumps[i];
        if (!pump.enabled || pump.outputType != OUTPUT_TYPE_PCF8574) continue;
        if (pump.expanderIndex == (uint8_t)expanderIndex) return true;
    }
    return false;
}

RelayManager::RelayManager() {
    memset(_pcfDevices, 0, sizeof(_pcfDevices));
    memset(_pcfOk,      0, sizeof(_pcfOk));
    memset(_runtime,    0, sizeof(_runtime));
    _stateMutex = xSemaphoreCreateMutex();
    if (!_stateMutex) {
        Serial.println("[Relay] ERROR: failed to create state mutex.");
    }
}

RelayManager::~RelayManager() {
    stopWatchdogTask();
    allOff();

    if (_stateMutex) {
        vSemaphoreDelete(_stateMutex);
        _stateMutex = nullptr;
    }

    for (int d = 0; d < MAX_EXPANDER_COUNT; d++) {
        delete _pcfDevices[d];
        _pcfDevices[d] = nullptr;
    }
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

void RelayManager::updatePumpStatusLocked(int index, const char* status) {
    if (index < 0 || index >= MAX_RELAY_COUNT) return;
    strlcpy(_runtime[index].lastStatus, status ? status : "", sizeof(_runtime[index].lastStatus));
}

void RelayManager::updatePumpErrorLocked(int index, const char* err) {
    if (index < 0 || index >= MAX_RELAY_COUNT) return;
    strlcpy(_runtime[index].lastError, err ? err : "", sizeof(_runtime[index].lastError));
}

bool RelayManager::setPumpOnLocked(int index, int slotIndex, int requestedDurationSec, const char* source) {
    if (index < 0 || index >= _config.relayCount) {
        Serial.printf("[Relay] activateRelay: index %d out of range.\n", index);
        return false;
    }

    const PumpEntry& p = _config.pumps[index];
    if (!p.enabled) {
        Serial.printf("[Relay] activateRelay: pump %d is disabled.\n", index);
        updatePumpErrorLocked(index, "pump disabled");
        updatePumpStatusLocked(index, "error");
        return false;
    }
    if (!isPumpValid(p)) {
        Serial.printf("[Relay] activateRelay: pump %d has invalid configuration.\n", index);
        updatePumpErrorLocked(index, "invalid pump configuration");
        updatePumpStatusLocked(index, "error");
        return false;
    }

    writeRelay(index, true);
    _relayState[index] = true;
    _runtime[index].running = true;
    _runtime[index].lastStartMs = millis();
    _runtime[index].lastStartEpoch = time(nullptr);
    _runtime[index].activeSlotIndex = slotIndex;
    _runtime[index].requestedDurationSec = requestedDurationSec;
    _runtime[index].maxRuntimeSec = p.maxRuntimeSec;
    _runtime[index].lastError[0] = '\0';
    updatePumpStatusLocked(index, "running");

    if (p.outputType == OUTPUT_TYPE_GPIO) {
        Serial.printf("[Relay] Pump %d (GPIO %d) ON [%s]\n", index, p.pin, source ? source : "n/a");
    } else {
        const char* expName = _config.expanders[p.expanderIndex].name;
        Serial.printf("[Relay] Pump %d (\"%s\" ch%d) ON [%s]\n",
                      index, expName, p.i2cChannel, source ? source : "n/a");
    }
    return true;
}

bool RelayManager::setPumpOffLocked(int index, const char* reason) {
    if (index < 0 || index >= _config.relayCount) return false;

    const PumpEntry& p = _config.pumps[index];
    if (!isPumpValid(p)) {
        updatePumpErrorLocked(index, "invalid pump configuration");
        updatePumpStatusLocked(index, "error");
        return false;
    }

    writeRelay(index, false);
    _relayState[index] = false;
    _testOffAt[index]  = 0;

    _runtime[index].running = false;
    _runtime[index].lastStopMs = millis();
    _runtime[index].lastStopEpoch = time(nullptr);
    _runtime[index].activeSlotIndex = -1;
    _runtime[index].requestedDurationSec = 0;
    updatePumpStatusLocked(index, "stopped");

    if (p.outputType == OUTPUT_TYPE_GPIO) {
        Serial.printf("[Relay] Pump %d (GPIO %d) OFF [%s]\n", index, p.pin, reason ? reason : "off");
    } else {
        const char* expName = _config.expanders[p.expanderIndex].name;
        Serial.printf("[Relay] Pump %d (\"%s\" ch%d) OFF [%s]\n",
                      index, expName, p.i2cChannel, reason ? reason : "off");
    }
    return true;
}

void RelayManager::watchdogTaskEntry(void* arg) {
    RelayManager* self = static_cast<RelayManager*>(arg);
    if (!self) {
        Serial.println("[Relay][Watchdog] ERROR: task started without RelayManager instance.");
        vTaskDelete(nullptr);
        return;
    }
    self->watchdogLoop();
    self->_watchdogTask = nullptr;
    vTaskDelete(nullptr);
}

void RelayManager::watchdogLoop() {
    const TickType_t delayTicks = pdMS_TO_TICKS(1000);
    unsigned long lastLockWarnAt = 0;
    while (_watchdogRun) {
        if (_stateMutex && xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            for (int i = 0; i < _config.relayCount; i++) {
                if (!_runtime[i].running) continue;
                int maxRuntimeSec = (_runtime[i].maxRuntimeSec > 0) ? _runtime[i].maxRuntimeSec : 0;
                if (maxRuntimeSec <= 0) continue;

                unsigned long elapsedMs = millis() - _runtime[i].lastStartMs;
                unsigned long maxMs = (unsigned long)maxRuntimeSec * 1000UL;
                if (elapsedMs >= maxMs) {
                    char err[96];
                    snprintf(err, sizeof(err), "runtime exceeded (%lu s/%u s)",
                             elapsedMs / 1000UL, (unsigned)maxRuntimeSec);
                    updatePumpErrorLocked(i, err);
                    setPumpOffLocked(i, "watchdog max runtime");
                    updatePumpStatusLocked(i, "watchdog-shutdown");
                    Serial.printf("[Relay][Watchdog] Pump %d forced OFF: %s\n", i, err);
                }
            }
            xSemaphoreGive(_stateMutex);
        } else {
            unsigned long now = millis();
            if (now - lastLockWarnAt > 5000UL) {
                Serial.println("[Relay][Watchdog] WARN: state mutex busy, retrying.");
                lastLockWarnAt = now;
            }
        }
        vTaskDelay(delayTicks);
    }
}

void RelayManager::ensureWatchdogTask() {
    if (_watchdogTask) return;
    _watchdogRun = true;

    const BaseType_t watchdogCore =
#if defined(ARDUINO_RUNNING_CORE)
        (ARDUINO_RUNNING_CORE == 0) ? 1 : 0;
#else
        0;
#endif

    BaseType_t ok = xTaskCreatePinnedToCore(
        RelayManager::watchdogTaskEntry,
        "pump-watchdog",
        4096,
        this,
        1,
        &_watchdogTask,
        watchdogCore
    );

    if (ok != pdPASS) {
        _watchdogTask = nullptr;
        _watchdogRun = false;
        Serial.println("[Relay] ERROR: could not start watchdog task.");
    } else {
        Serial.printf("[Relay] Watchdog task started on core %d.\n", (int)watchdogCore);
    }
}

void RelayManager::stopWatchdogTask() {
    _watchdogRun = false;
    if (_watchdogTask) {
        // Give the task a short window to leave its loop and self-delete.
        for (int i = 0; i < WATCHDOG_SHUTDOWN_MAX_RETRIES && _watchdogTask; i++) {
            vTaskDelay(pdMS_TO_TICKS(WATCHDOG_SHUTDOWN_RETRY_DELAY_MS));
        }
        if (_watchdogTask) {
            TaskHandle_t handle = _watchdogTask;
            _watchdogTask = nullptr;
            vTaskDelete(handle);
        }
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

void RelayManager::begin(HardwareConfig& config) {
    stopWatchdogTask();

    bool stateLocked = false;
    if (_stateMutex && xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        stateLocked = true;
    } else if (_stateMutex) {
        Serial.println("[Relay] WARN: begin() proceeding without state mutex lock.");
    }
    _config = config;
    _config.relayCount = constrain(_config.relayCount, 0, MAX_RELAY_COUNT);
    _config.expanderCount = constrain(_config.expanderCount, 0, MAX_EXPANDER_COUNT);

    // Reset runtime state snapshot and pending test deadlines.
    for (int i = 0; i < MAX_RELAY_COUNT; i++) {
        _testOffAt[i] = 0;
        _relayState[i] = false;
        memset(&_runtime[i], 0, sizeof(_runtime[i]));
        _runtime[i].activeSlotIndex = -1;
        updatePumpStatusLocked(i, "idle");
    }
    if (_stateMutex && stateLocked) xSemaphoreGive(_stateMutex);

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
    // Delete any objects from a previous begin() call and create fresh instances
    // so that Adafruit_PCF8574::begin() always starts with i2c_dev == NULL.
    for (int d = 0; d < MAX_EXPANDER_COUNT; d++) {
        delete _pcfDevices[d];
        _pcfDevices[d] = nullptr;
        _pcfOk[d]      = false;
    }
    for (int d = 0; d < _config.expanderCount; d++) {
        const ExpanderEntry& e = _config.expanders[d];
        if (!e.enabled) continue;
        if (!expanderHasAssignedPump(_config, d)) {
            Serial.printf("[Relay] Skipping expander \"%s\" (0x%02X) - no pump assigned yet.\n",
                          e.name, e.i2cAddress);
            continue;
        }
        _pcfDevices[d] = new (std::nothrow) Adafruit_PCF8574();
        if (!_pcfDevices[d]) {
            Serial.printf("[Relay] ERROR: out of memory allocating PCF device %d\n", d);
            continue;
        }
        _pcfOk[d] = _pcfDevices[d]->begin(e.i2cAddress);
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

    // Final safe-state pass for all configured outputs
    stateLocked = false;
    if (_stateMutex && xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        stateLocked = true;
    } else if (_stateMutex) {
        Serial.println("[Relay] WARN: begin() safe-state pass without mutex lock.");
    }
    for (int i = 0; i < _config.relayCount; i++) {
        if (isPumpValid(_config.pumps[i])) {
            setPumpOffLocked(i, "init");
        }
    }
    if (_stateMutex && stateLocked) xSemaphoreGive(_stateMutex);

    ensureWatchdogTask();

    Serial.printf("[Relay] Initialized %d pump(s), %d expander(s).\n",
                  _config.relayCount, _config.expanderCount);
}

bool RelayManager::activateRelay(int index, bool armed, int slotIndex, int requestedDurationSec) {
    if (!armed) {
        Serial.printf("[Relay] Activate pump %d blocked – not armed (safety lock).\n", index);
        if (_stateMutex && xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            updatePumpErrorLocked(index, "not armed");
            updatePumpStatusLocked(index, "blocked");
            xSemaphoreGive(_stateMutex);
        }
        return false;
    }

    bool ok = false;
    if (_stateMutex && xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        ok = setPumpOnLocked(index, slotIndex, requestedDurationSec, "scheduler");
        xSemaphoreGive(_stateMutex);
    }
    return ok;
}

bool RelayManager::deactivateRelay(int index, const char* reason) {
    bool ok = false;
    if (_stateMutex && xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        ok = setPumpOffLocked(index, reason);
        xSemaphoreGive(_stateMutex);
    }
    return ok;
}

bool RelayManager::stopSafely(int index, const char* reason) {
    return deactivateRelay(index, reason ? reason : "safe stop");
}

void RelayManager::allOff() {
    if (_stateMutex && xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        for (int i = 0; i < MAX_RELAY_COUNT; i++) {
            if (i < _config.relayCount && isPumpValid(_config.pumps[i])) {
                setPumpOffLocked(i, "all off");
            } else {
                _relayState[i] = false;
                _testOffAt[i]  = 0;
                _runtime[i].running = false;
                _runtime[i].activeSlotIndex = -1;
                updatePumpStatusLocked(i, "idle");
            }
        }
        xSemaphoreGive(_stateMutex);
    }
    Serial.println("[Relay] All pumps OFF.");
}

bool RelayManager::isActive(int index) const {
    if (index < 0 || index >= MAX_RELAY_COUNT) return false;
    bool active = false;
    if (_stateMutex && xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        active = _relayState[index];
        xSemaphoreGive(_stateMutex);
    } else {
        // Best-effort fallback: return cached state when mutex is temporarily busy.
        active = _relayState[index];
        Serial.printf("[Relay] WARN: isActive(%d) using cached state (mutex busy).\n", index);
    }
    return active;
}

bool RelayManager::getPumpRuntimeInfo(int index, PumpRuntimeInfo& out) const {
    if (index < 0 || index >= MAX_RELAY_COUNT) return false;
    if (_stateMutex && xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        out = _runtime[index];
        xSemaphoreGive(_stateMutex);
        return true;
    }
    return false;
}

bool RelayManager::testActivateRelay(int index) {
    bool ok = false;
    if (_stateMutex && xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        if (index < 0 || index >= _config.relayCount) {
            Serial.printf("[Relay] testActivateRelay: index %d out of range.\n", index);
            xSemaphoreGive(_stateMutex);
            return false;
        }

        const PumpEntry& p = _config.pumps[index];
        int timeout = (p.maxRuntimeSec > 0) ? p.maxRuntimeSec : 30;
        ok = setPumpOnLocked(index, -1, timeout, "web-test");
        if (ok) {
            _testOffAt[index] = millis() + ((unsigned long)timeout * 1000UL);
            if (p.outputType == OUTPUT_TYPE_GPIO) {
                Serial.printf("[Relay] TEST pump %d (GPIO %d) ON – auto-off in %ds\n",
                              index, p.pin, timeout);
            } else {
                const char* expName = _config.expanders[p.expanderIndex].name;
                Serial.printf("[Relay] TEST pump %d (\"%s\" ch%d) ON – auto-off in %ds\n",
                              index, expName, p.i2cChannel, timeout);
            }
        }
        xSemaphoreGive(_stateMutex);
    }
    return ok;
}

bool RelayManager::testDeactivateRelay(int index) {
    bool ok = false;
    if (_stateMutex && xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        ok = setPumpOffLocked(index, "web-test off");
        xSemaphoreGive(_stateMutex);
    }
    return ok;
}

void RelayManager::update() {
    unsigned long now = millis();
    // MILLIS_HALF_RANGE: half of unsigned long range; used for wraparound-safe
    // comparison after ~49 days when millis() rolls over.
    static const unsigned long MILLIS_HALF_RANGE = 0x80000000UL;

    if (_stateMutex && xSemaphoreTake(_stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < _config.relayCount; i++) {
            // Keep test auto-off behavior in main loop for immediate UX feedback;
            // watchdog task on the other core remains the independent failsafe.
            if (_testOffAt[i] != 0 && (now - _testOffAt[i]) < MILLIS_HALF_RANGE) {
                setPumpOffLocked(i, "test timeout");
                Serial.printf("[Relay] TEST pump %d auto-off (timeout reached)\n", i);
            }
        }
        xSemaphoreGive(_stateMutex);
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
        if (di >= MAX_EXPANDER_COUNT || !_pcfOk[di] || !_pcfDevices[di]) {
            Serial.printf("[Relay] writeRelay: expander %d not available.\n", di);
            return;
        }
        uint8_t maxChannel = (_config.expanders[di].chipType == EXPANDER_TYPE_PCF8575) ? 15 : 7;
        if (p.i2cChannel > maxChannel) {
            Serial.printf("[Relay] writeRelay: channel %d out of range for expander %d.\n",
                          p.i2cChannel, di);
            return;
        }
        _pcfDevices[di]->digitalWrite(p.i2cChannel, level);
    }
}
