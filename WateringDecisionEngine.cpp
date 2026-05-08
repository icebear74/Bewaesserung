#include "WateringDecisionEngine.h"
#include <time.h>

static void setText(char* dst, size_t dstSize, const char* text) {
    if (!dst || dstSize == 0) return;
    strlcpy(dst, text ? text : "", dstSize);
}

static bool weatherPolicyIsActive(const WeatherPolicy& policy) {
    return policy.skipIfRainMm > 0.0f ||
           policy.skipIfRainPct > 0.0f ||
           policy.runOnlyAboveTemp > -99.0f ||
           policy.reduceIfRainMm > 0.0f;
}

static int daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);                       // [0, 399]
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; // [0, 365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;           // [0, 146096]
    return era * 146097 + (int)doe - 719468;
}

static int localEpochDay(time_t localTs) {
    struct tm t;
    localtime_r(&localTs, &t);
    return daysFromCivil(t.tm_year + 1900, (unsigned)(t.tm_mon + 1), (unsigned)t.tm_mday);
}

static bool dayMatches(const WateringSlot& slot, time_t localTs) {
    struct tm t;
    localtime_r(&localTs, &t);
    if (slot.repeatMode == REPEAT_INTERVAL_DAYS) {
        // Defensive clamp: persisted/manual JSON edits can bypass UI validation.
        int every = slot.intervalDays < 1 ? 1 : slot.intervalDays;
        int today = localEpochDay(localTs);
        int anchor = slot.intervalAnchorDay;
        int delta = today - anchor;
        return (delta >= 0) && ((delta % every) == 0);
    }
    int dow = (t.tm_wday + 6) % 7;  // Monday=0
    return (slot.days & (1 << dow)) != 0;
}

static void fillTriggerSource(char* dst, size_t dstSize,
                              const WateringSlot& slot,
                              bool usedFallbackTime) {
    if (slot.triggerType == TRIGGER_FIXED_TIME) {
        snprintf(dst, dstSize, "Feste Uhrzeit %02u:%02u", slot.fixedHour, slot.fixedMinute);
        return;
    }
    if (slot.triggerType == TRIGGER_SUNRISE) {
        snprintf(dst, dstSize, usedFallbackTime
                                  ? "Sonnenaufgang (Fallback %02u:%02u)"
                                  : "Sonnenaufgang",
                 slot.fixedHour, slot.fixedMinute);
        return;
    }
    if (slot.triggerType == TRIGGER_SUNSET) {
        snprintf(dst, dstSize, usedFallbackTime
                                  ? "Sonnenuntergang (Fallback %02u:%02u)"
                                  : "Sonnenuntergang",
                 slot.fixedHour, slot.fixedMinute);
        return;
    }
    if (slot.triggerType == TRIGGER_MIDDAY) {
        snprintf(dst, dstSize, usedFallbackTime
                                  ? "Mitte zwischen Sonnenauf/-untergang (Fallback %02u:%02u)"
                                  : "Mitte zwischen Sonnenauf/-untergang",
                 slot.fixedHour, slot.fixedMinute);
        return;
    }
    const char* base = "Sonnenaufgang";
    if (slot.offsetBase == OFFSET_BASE_SUNSET) base = "Sonnenuntergang";
    else if (slot.offsetBase == OFFSET_BASE_MIDDAY) base = "Mittagszeit";
    snprintf(dst, dstSize, usedFallbackTime
                              ? "Offset %+d Min relativ zu %s (Fallback %02u:%02u)"
                              : "Offset %+d Min relativ zu %s",
             slot.offsetMinutes, base, slot.fixedHour, slot.fixedMinute);
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

    out.dayMatched = dayMatches(slot, input.nowLocal);
    if (input.enforceDayMatch && !out.dayMatched) {
        if (slot.repeatMode == REPEAT_INTERVAL_DAYS) {
            setText(out.reason, sizeof(out.reason), "Slot ist heute laut Intervallregel nicht aktiv.");
        } else {
            setText(out.reason, sizeof(out.reason), "Slot ist heute nicht aktiv.");
        }
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
    fillTriggerSource(out.triggerSource, sizeof(out.triggerSource), slot, out.usedFallbackTime);

    struct tm nowTm;
    struct tm trigTm;
    localtime_r(&input.nowLocal, &nowTm);
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
            setText(out.warnings, sizeof(out.warnings), "Wetterdaten sind veraltet.");
        }
    }

    int runnableCount = 0;
    int reducedCount  = 0;
    int skippedCount  = 0;

    for (int ai = 0; ai < input.slotConfig->assignCount && out.planCount < MAX_SLOT_ASSIGNMENTS; ai++) {
        const SlotPumpAssignment& asgn = input.slotConfig->assignments[ai];
        if (asgn.slotIndex != (uint8_t)input.slotIndex) continue;
        if (asgn.pumpIndex >= (uint8_t)input.hardwareConfig->relayCount) continue;

        WateringDecisionPumpPlan& p = out.plan[out.planCount++];
        p.assignmentIndex = (uint8_t)ai;
        p.pumpIndex       = asgn.pumpIndex;
        p.baseDurationSec = asgn.durationSec;
        p.durationSec     = asgn.durationSec;
        p.action          = WATER_ACTION_EXECUTE;
        setText(p.reason, sizeof(p.reason), "Wird ausgeführt.");
        setText(p.policySource, sizeof(p.policySource), "none");

        if (!input.hardwareConfig->pumps[p.pumpIndex].enabled) {
            p.action = WATER_ACTION_SKIP;
            p.durationSec = 0;
            setText(p.reason, sizeof(p.reason), "Pumpe ist deaktiviert.");
            skippedCount++;
            continue;
        }

        WeatherPolicy policy;
        if (asgn.weatherTemplateIndex >= 0 &&
            asgn.weatherTemplateIndex < input.slotConfig->weatherTemplateCount) {
            policy = input.slotConfig->weatherTemplates[asgn.weatherTemplateIndex].weather;
            setText(p.policySource, sizeof(p.policySource), "template");
        } else if (asgn.useOwnWeatherPolicy) {
            policy = asgn.weather;
            setText(p.policySource, sizeof(p.policySource), "assignment-legacy");
        } else {
            policy.skipIfRainMm = slot.skipIfRainMm;
            policy.skipIfRainPct = slot.skipIfRainPct;
            policy.runOnlyAboveTemp = slot.runOnlyAboveTemp;
            policy.reduceIfRainMm = slot.reduceIfRainMm;
            policy.reducePct = slot.reducePct;
            if (weatherPolicyIsActive(policy)) {
                setText(p.policySource, sizeof(p.policySource), "slot-legacy");
            }
        }

        if (input.weatherAvailable && input.weatherData && weatherPolicyIsActive(policy)) {
            const WeatherData& w = *input.weatherData;
            if (policy.skipIfRainMm > 0.0f && w.dailyPrecipMm >= policy.skipIfRainMm) {
                p.action = WATER_ACTION_SKIP;
                p.durationSec = 0;
                snprintf(p.reason, sizeof(p.reason), "Ausgesetzt: Regen %.1f mm >= %.1f mm.",
                         w.dailyPrecipMm, policy.skipIfRainMm);
                skippedCount++;
                continue;
            }
            if (policy.skipIfRainPct > 0.0f && w.dailyPrecipPct >= policy.skipIfRainPct) {
                p.action = WATER_ACTION_SKIP;
                p.durationSec = 0;
                snprintf(p.reason, sizeof(p.reason),
                         "Ausgesetzt: Regenwahrscheinlichkeit %.0f%% >= %.0f%%.",
                         w.dailyPrecipPct, policy.skipIfRainPct);
                skippedCount++;
                continue;
            }
            if (policy.runOnlyAboveTemp > -99.0f && w.temperature < policy.runOnlyAboveTemp) {
                p.action = WATER_ACTION_SKIP;
                p.durationSec = 0;
                snprintf(p.reason, sizeof(p.reason), "Ausgesetzt: Temperatur %.1f°C < %.1f°C.",
                         w.temperature, policy.runOnlyAboveTemp);
                skippedCount++;
                continue;
            }
            if (policy.reduceIfRainMm > 0.0f && w.dailyPrecipMm >= policy.reduceIfRainMm) {
                int d = p.durationSec * (100 - (int)policy.reducePct) / 100;
                p.durationSec = d < 1 ? 1 : d;
                if (p.durationSec < p.baseDurationSec) {
                    p.action = WATER_ACTION_REDUCE;
                    snprintf(p.reason, sizeof(p.reason), "Reduziert: Regen %.1f mm >= %.1f mm.",
                             w.dailyPrecipMm, policy.reduceIfRainMm);
                    reducedCount++;
                }
            }
        }

        if (p.action != WATER_ACTION_SKIP) {
            runnableCount++;
            out.totalDurationSec += p.durationSec;
        }
    }

    if (out.planCount == 0) {
        setText(out.reason, sizeof(out.reason), "Keine Pumpenzuweisung für den Slot.");
        out.action = WATER_ACTION_SKIP;
        return out;
    }

    if (runnableCount == 0) {
        setText(out.reason, sizeof(out.reason), "Alle zugewiesenen Pumpen werden ausgesetzt.");
        out.action = WATER_ACTION_SKIP;
    } else if (reducedCount > 0) {
        setText(out.reason, sizeof(out.reason), "Slot wird mit teilweise reduzierter Laufzeit ausgeführt.");
        out.action = WATER_ACTION_REDUCE;
    } else if (out.usedFallbackTime) {
        setText(out.reason, sizeof(out.reason), "Slot läuft mit Fallback-Uhrzeit (ohne Astro-Daten).");
        out.action = WATER_ACTION_FALLBACK;
    } else if (skippedCount > 0) {
        setText(out.reason, sizeof(out.reason), "Slot wird ausgeführt (teilweise ausgesetzt).");
        out.action = WATER_ACTION_EXECUTE;
    } else {
        setText(out.reason, sizeof(out.reason), "Slot wird normal ausgeführt.");
        out.action = WATER_ACTION_EXECUTE;
    }

    return out;
}
