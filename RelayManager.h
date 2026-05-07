#pragma once
#include <Arduino.h>
#include <Adafruit_PCF8574.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <time.h>
#include "ConfigManager.h"

class RelayManager {
public:
    struct PumpRuntimeInfo {
        bool          running             = false;
        unsigned long lastStartMs         = 0;
        unsigned long lastStopMs          = 0;
        time_t        lastStartEpoch      = 0;
        time_t        lastStopEpoch       = 0;
        int           activeSlotIndex     = -1;
        int           requestedDurationSec= 0;
        int           maxRuntimeSec       = 0;
        char          lastStatus[64]      = "idle";
        char          lastError[96]       = "";
    };

    RelayManager();
    ~RelayManager();
    void begin(HardwareConfig& config);

    // Scheduled watering: only activates if armed (safety lock)
    bool activateRelay(int index, bool armed, int slotIndex = -1, int requestedDurationSec = 0);
    bool deactivateRelay(int index, const char* reason = "off");
    bool stopSafely(int index, const char* reason);
    void allOff();
    bool isActive(int index) const;
    bool getPumpRuntimeInfo(int index, PumpRuntimeInfo& out) const;
    int  getRelayCount() const { return _config.relayCount; }

    // Test control: no armed check, bounded by maxRuntimeSec timeout
    bool testActivateRelay(int index);
    bool testDeactivateRelay(int index);

    // Call from main loop to handle test timeouts
    void update();

private:
    void writeRelay(int index, bool on);
    bool setPumpOnLocked(int index, int slotIndex, int requestedDurationSec, const char* source);
    bool setPumpOffLocked(int index, const char* reason);
    void updatePumpStatusLocked(int index, const char* status);
    void updatePumpErrorLocked(int index, const char* err);
    static void watchdogTaskEntry(void* arg);
    void watchdogLoop();
    void ensureWatchdogTask();
    void stopWatchdogTask();
    // Returns true when a pump entry has a valid output configuration
    bool isPumpValid(const PumpEntry& p) const;

    HardwareConfig   _config;
    bool             _relayState[MAX_RELAY_COUNT]  = {false};
    unsigned long    _testOffAt[MAX_RELAY_COUNT]   = {0};
    PumpRuntimeInfo  _runtime[MAX_RELAY_COUNT];

    // One PCF device per expander entry; indexed directly by ExpanderEntry index.
    // Stored as pointers so fresh objects are created on each begin() call,
    // guaranteeing the Adafruit_PCF8574 constructor runs on clean heap memory and
    // i2c_dev is always NULL before begin() is invoked (prevents heap-poison crash
    // on library versions that call `delete i2c_dev` unconditionally).
    Adafruit_PCF8574* _pcfDevices[MAX_EXPANDER_COUNT];
    bool              _pcfOk[MAX_EXPANDER_COUNT];
    SemaphoreHandle_t _stateMutex = nullptr;
    TaskHandle_t      _watchdogTask = nullptr;
    bool              _watchdogRun = false;
};
