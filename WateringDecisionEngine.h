#pragma once

#include <Arduino.h>
#include "ConfigManager.h"

// Shared slot decision outcomes used by live scheduling and simulation.
enum WateringDecisionAction : uint8_t {
    WATER_ACTION_SKIP = 0,
    WATER_ACTION_EXECUTE,
    WATER_ACTION_REDUCE,
    WATER_ACTION_FALLBACK
};

struct WateringDecisionPumpPlan {
    uint8_t assignmentIndex = 0;
    uint8_t pumpIndex       = 0;
    int     baseDurationSec = 0;
    int     durationSec     = 0;
    WateringDecisionAction action = WATER_ACTION_SKIP;
    char    reason[120] = {0};
    char    policySource[32] = {0};
};

struct WateringDecisionInput {
    const SlotConfig*     slotConfig      = nullptr;
    const HardwareConfig* hardwareConfig  = nullptr;
    const WeatherData*    weatherData     = nullptr;
    bool                  weatherAvailable = false;
    bool                  weatherStale     = false;
    time_t                nowLocal         = 0;
    int                   slotIndex        = -1;
    bool                  enforceDayMatch  = true;
    bool                  enforceTriggerMinute = true;
};

struct WateringDecisionResult {
    bool                  validInput       = false;
    int                   slotIndex        = -1;
    bool                  dayMatched       = false;
    bool                  triggerMatched   = false;
    bool                  usedFallbackTime = false;
    time_t                triggerTime      = 0;
    char                  triggerSource[96] = {0};
    WateringDecisionAction action          = WATER_ACTION_SKIP;
    char                  reason[160]      = {0};
    char                  weatherJustification[192] = {0};
    char                  warnings[160]    = {0};
    int                   totalDurationSec = 0;
    int                   planCount        = 0;
    WateringDecisionPumpPlan plan[MAX_SLOT_ASSIGNMENTS];
};

class WateringDecisionEngine {
public:
    static time_t computeTriggerTime(const WateringSlot& slot,
                                     time_t localNow,
                                     const WeatherData* weatherData,
                                     bool weatherAvailable,
                                     bool* usedFallbackTime);

    static WateringDecisionResult evaluateSlot(const WateringDecisionInput& input);
};
