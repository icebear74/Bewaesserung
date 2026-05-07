#pragma once
#include <Arduino.h>
#include "ConfigManager.h"
#include "RelayManager.h"
#include "WeatherManager.h"

// Maximum number of pump activations that can be queued at once
#define SCHEDULER_QUEUE_SIZE  MAX_SLOT_ASSIGNMENTS

class WateringScheduler {
public:
    WateringScheduler();

    void begin(ConfigManager* cfg, RelayManager* rm, WeatherManager* wm);
    void update();  // call from main loop every iteration

    // True while at least one pump is active or the queue is non-empty
    bool isBusy() const;

    // Index of the currently active pump (-1 if none)
    int  getActivePump() const { return _activePump; }

private:
    struct QueueItem {
        uint8_t pumpIndex   = 0;
        int     durationSec = 0;
        uint8_t slotIndex   = 0;
    };

    // Compute local trigger time for a slot; returns 0 on error
    time_t computeTriggerTime(const WateringSlot& slot, time_t localNow) const;

    // Returns true if weather conditions allow the slot to run
    bool shouldRunSlot(const WateringSlot& slot) const;

    // Returns effective duration, possibly reduced by weather
    int  computeDuration(const WateringSlot& slot, int baseDuration) const;

    // Add all pump assignments for a slot to the queue
    void enqueueSlot(int slotIdx);

    ConfigManager*  _cfg        = nullptr;
    RelayManager*   _rm         = nullptr;
    WeatherManager* _wm         = nullptr;

    // Circular queue for pump runs
    QueueItem     _queue[SCHEDULER_QUEUE_SIZE];
    int           _qHead        = 0;
    int           _qTail        = 0;

    bool          _pumpRunning  = false;
    int           _activePump   = -1;
    unsigned long _pumpOffAt    = 0;

    // Track the last minute we checked to avoid double-firing
    time_t        _lastCheckedMinute = 0;
};
