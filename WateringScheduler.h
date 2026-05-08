#pragma once
#include <Arduino.h>
#include <esp_heap_caps.h>
#include "ConfigManager.h"
#include "RelayManager.h"
#include "WeatherManager.h"
#include "WateringDecisionEngine.h"

// Maximum number of pump activations that can be queued at once
#define SCHEDULER_QUEUE_SIZE  MAX_SLOT_ASSIGNMENTS

class WateringScheduler {
public:
    WateringScheduler();
    ~WateringScheduler();

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

    // Add one evaluated execution plan to the queue
    void enqueueDecision(const WateringDecisionResult& decision);

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

    // Reusable decision result buffer allocated in PSRAM to avoid stack overflow.
    // WateringDecisionResult is ~14 KB; the loopTask stack is only 8 KB.
    WateringDecisionResult* _decisionBuf = nullptr;
};
