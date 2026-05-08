#include "WateringDecisionEngine.h"
#include <time.h>

static void setText(char* dst, size_t dstSize, const char* text) {
    if (!dst || dstSize == 0) return;
    strlcpy(dst, text ? text : "", dstSize);
}

time_t WateringDecisionEngine::computeTriggerTime(const WateringSlot& slot,
                                                  time_t localNow,
                                                  const WeatherData* weatherData,
                                                  bool weatherAvailable,
                                                  bool* usedFallbackTime) {
    if (usedFallbackTime) *usedFallbackTime = false;

    auto buildFixed = [&]() -> time_t {
        struct tm lt;
        localtime_r(&localNow, &lt);
        lt.tm_hour  = slot.fixedHour;
        lt.tm_min   = slot.fixedMinute;
        lt.tm_sec   = 0;
        lt.tm_isdst = -1;
        time_t t = mktime(&lt);
        return (t == (time_t)-1) ? 0 : t;
    };

    if (slot.triggerType == TRIGGER_FIXED_TIME) {
        return buildFixed();
    }

    bool haveAstro = weatherAvailable && weatherData &&
                     weatherData->sunrise != 0 && weatherData->sunset != 0;

    if (!haveAstro) {
        if (usedFallbackTime) *usedFallbackTime = true;
        return buildFixed();
    }

    time_t base = 0;
    switch (slot.triggerType) {
        case TRIGGER_SUNRISE: base = weatherData->sunrise; break;
        case TRIGGER_SUNSET:  base = weatherData->sunset;  break;
        case TRIGGER_MIDDAY:  base = (weatherData->sunrise + weatherData->sunset) / 2; break;
        case TRIGGER_OFFSET:
            switch (slot.offsetBase) {
                case OFFSET_BASE_SUNRISE: base = weatherData->sunrise; break;
                case OFFSET_BASE_SUNSET:  base = weatherData->sunset;  break;
                case OFFSET_BASE_MIDDAY:  base = (weatherData->sunrise + weatherData->sunset) / 2; break;
                default:                  base = weatherData->sunrise; break;
            }
            return (base != 0) ? base + (time_t)slot.offsetMinutes * 60 : buildFixed();
        default:
            break;
    }

    return (base != 0) ? base : buildFixed();
}

WateringDecisionResult WateringDecisionEngine::evaluateSlot(const WateringDecisionInput& input) {
    WateringDecisionResult out;
    out.slotIndex = input.slotIndex;

    if (!input.slotConfig || !input.hardwareConfig ||
        input.slotIndex < 0 || input.slotIndex >= input.slotConfig->slotCount) {
        setText(out.reason, sizeof(out.reason), "Ungültige Evaluationsdaten.");
        return out;
    }

    out.validInput = true;
    const WateringSlot& slot = input.slotConfig->slots[input.slotIndex];

    if (!slot.enabled) {
        setText(out.reason, sizeof(out.reason), "Slot ist deaktiviert.");
        return out;
    }

    if (input.nowLocal < 1000000L) {
        setText(out.reason, sizeof(out.reason), "Systemzeit ist nicht gesetzt.");
        return out;
    }

    struct tm nowTm;
    localtime_r(&input.nowLocal, &nowTm);
    int dow = (nowTm.tm_wday + 6) % 7;
    out.dayMatched = (slot.days & (1 << dow)) != 0;
    if (input.enforceDayMatch && !out.dayMatched) {
        setText(out.reason, sizeof(out.reason), "Slot ist heute nicht aktiv.");
        return out;
    }

    out.triggerTime = computeTriggerTime(slot,
                                         input.nowLocal,
                                         input.weatherData,
                                         input.weatherAvailable,
                                         &out.usedFallbackTime);
    if (out.triggerTime == 0) {
        setText(out.reason, sizeof(out.reason), "Auslöserzeit konnte nicht berechnet werden.");
        return out;
    }

    struct tm trigTm;
    localtime_r(&out.triggerTime, &trigTm);
    out.triggerMatched = (nowTm.tm_hour == trigTm.tm_hour && nowTm.tm_min == trigTm.tm_min);
    if (input.enforceTriggerMinute && !out.triggerMatched) {
        setText(out.reason, sizeof(out.reason), "Slot ist zur gewählten Zeit nicht fällig.");
        return out;
    }

    if (!input.weatherAvailable || !input.weatherData) {
        setText(out.weatherJustification, sizeof(out.weatherJustification),
                "Keine Wetterdaten verfügbar – Wetterregeln bleiben inaktiv.");
        if (out.usedFallbackTime) {
            setText(out.warnings, sizeof(out.warnings),
                    "Astronomische Trigger nutzen Fallback-Uhrzeit ohne Wetterdaten.");
        }
    } else {
        const WeatherData& w = *input.weatherData;
        char wb[192];
        snprintf(wb, sizeof(wb),
                 "Wetter: %.1f°C, Regen heute %.1f mm, Regenwahrscheinlichkeit %.0f%%.",
                 w.temperature, w.dailyPrecipMm, w.dailyPrecipPct);
        setText(out.weatherJustification, sizeof(out.weatherJustification), wb);

        if (input.weatherStale) {
            setText(out.warnings, sizeof(out.warnings),
                    "Wetterdaten sind veraltet.");
        }

        if (slot.skipIfRainMm > 0.0f && w.dailyPrecipMm >= slot.skipIfRainMm) {
            snprintf(out.reason, sizeof(out.reason),
                     "Ausgesetzt: Regen %.1f mm >= %.1f mm.",
                     w.dailyPrecipMm, slot.skipIfRainMm);
            out.action = WATER_ACTION_SKIP;
            return out;
        }
        if (slot.skipIfRainPct > 0.0f && w.dailyPrecipPct >= slot.skipIfRainPct) {
            snprintf(out.reason, sizeof(out.reason),
                     "Ausgesetzt: Regenwahrscheinlichkeit %.0f%% >= %.0f%%.",
                     w.dailyPrecipPct, slot.skipIfRainPct);
            out.action = WATER_ACTION_SKIP;
            return out;
        }
        if (slot.runOnlyAboveTemp > -99.0f && w.temperature < slot.runOnlyAboveTemp) {
            snprintf(out.reason, sizeof(out.reason),
                     "Ausgesetzt: Temperatur %.1f°C < %.1f°C.",
                     w.temperature, slot.runOnlyAboveTemp);
            out.action = WATER_ACTION_SKIP;
            return out;
        }
    }

    bool reduced = false;
    for (int ai = 0; ai < input.slotConfig->assignCount && out.planCount < MAX_SLOT_ASSIGNMENTS; ai++) {
        const SlotPumpAssignment& asgn = input.slotConfig->assignments[ai];
        if (asgn.slotIndex != (uint8_t)input.slotIndex) continue;
        if (asgn.pumpIndex >= (uint8_t)input.hardwareConfig->relayCount) continue;

        WateringDecisionPumpPlan& p = out.plan[out.planCount++];
        p.assignmentIndex = (uint8_t)ai;
        p.pumpIndex       = asgn.pumpIndex;
        p.baseDurationSec = asgn.durationSec;
        p.durationSec     = asgn.durationSec;

        if (input.weatherAvailable && input.weatherData && slot.reduceIfRainMm > 0.0f &&
            input.weatherData->dailyPrecipMm >= slot.reduceIfRainMm) {
            int d = p.durationSec * (100 - (int)slot.reducePct) / 100;
            p.durationSec = d < 1 ? 1 : d;
            if (p.durationSec < p.baseDurationSec) reduced = true;
        }

        out.totalDurationSec += p.durationSec;
    }

    if (out.planCount == 0) {
        setText(out.reason, sizeof(out.reason), "Keine Pumpenzuweisung für den Slot.");
        out.action = WATER_ACTION_SKIP;
        return out;
    }

    if (reduced) {
        setText(out.reason, sizeof(out.reason), "Slot wird mit reduzierter Laufzeit ausgeführt.");
        out.action = WATER_ACTION_REDUCE;
    } else if (out.usedFallbackTime) {
        setText(out.reason, sizeof(out.reason), "Slot läuft mit Fallback-Uhrzeit (ohne Astro-Daten).");
        out.action = WATER_ACTION_FALLBACK;
    } else {
        setText(out.reason, sizeof(out.reason), "Slot wird normal ausgeführt.");
        out.action = WATER_ACTION_EXECUTE;
    }

    return out;
}
