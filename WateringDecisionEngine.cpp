#include "WateringDecisionEngine.h"
#include <math.h>
#include <time.h>

static void setText(char* dst, size_t dstSize, const char* text) {
    if (!dst || dstSize == 0) return;
    strlcpy(dst, text ? text : "", dstSize);
}

static void appendText(char* dst, size_t dstSize, const char* text, const char* separator = "; ") {
    if (!dst || dstSize == 0 || !text || !text[0]) return;
    size_t used = strlen(dst);
    if (used > 0 && separator && separator[0]) {
        strlcat(dst, separator, dstSize);
    }
    strlcat(dst, text, dstSize);
}

static bool weatherPolicyIsActive(const WeatherPolicy& policy) {
    return policy.skipIfRainMm > 0.0f ||
           policy.skipIfRainPct > 0.0f ||
           policy.runOnlyAboveTemp > -99.0f ||
           policy.reduceIfRainMm > 0.0f;
}

static bool weatherRuleIsActive(const WeatherRule& rule) {
    if (!rule.enabled) return false;
    if (rule.actionType == WEATHER_RULE_SKIP) return true;
    return rule.effectPercent > 0;
}

static void clearWeatherTemplateRules(WeatherTemplate& wt) {
    wt.ruleCount = 0;
    for (int i = 0; i < MAX_WEATHER_RULES_PER_TEMPLATE; i++) {
        wt.rules[i] = WeatherRule{};
    }
}

static void addWeatherRule(WeatherTemplate& wt, const WeatherRule& rule) {
    if (wt.ruleCount >= MAX_WEATHER_RULES_PER_TEMPLATE) return;
    wt.rules[wt.ruleCount++] = rule;
}

static void appendLegacyPolicyRules(WeatherTemplate& wt, const WeatherPolicy& policy) {
    clearWeatherTemplateRules(wt);

    if (policy.skipIfRainMm > 0.0f) {
        WeatherRule rule;
        rule.actionType = WEATHER_RULE_SKIP;
        rule.metric = WEATHER_METRIC_DAILY_RAIN_MM;
        rule.comparison = WEATHER_OP_GTE;
        rule.threshold = policy.skipIfRainMm;
        addWeatherRule(wt, rule);
    }
    if (policy.skipIfRainPct > 0.0f) {
        WeatherRule rule;
        rule.actionType = WEATHER_RULE_SKIP;
        rule.metric = WEATHER_METRIC_DAILY_RAIN_PROB;
        rule.comparison = WEATHER_OP_GTE;
        rule.threshold = policy.skipIfRainPct;
        addWeatherRule(wt, rule);
    }
    if (policy.runOnlyAboveTemp > -99.0f) {
        WeatherRule rule;
        rule.actionType = WEATHER_RULE_SKIP;
        rule.metric = WEATHER_METRIC_CURRENT_TEMP;
        rule.comparison = WEATHER_OP_LT;
        rule.threshold = policy.runOnlyAboveTemp;
        addWeatherRule(wt, rule);
    }
    if (policy.reduceIfRainMm > 0.0f) {
        WeatherRule rule;
        rule.actionType = WEATHER_RULE_REDUCE_RUNTIME;
        rule.metric = WEATHER_METRIC_DAILY_RAIN_MM;
        rule.comparison = WEATHER_OP_GTE;
        rule.threshold = policy.reduceIfRainMm;
        rule.effectPercent = constrain((int)policy.reducePct, 1, 99);
        addWeatherRule(wt, rule);
    }
}

static bool weatherTemplateHasRules(const WeatherTemplate& wt) {
    for (int i = 0; i < wt.ruleCount; i++) {
        if (weatherRuleIsActive(wt.rules[i])) return true;
    }
    return false;
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

static bool metricUsesWindow(uint8_t metric) {
    return metric == WEATHER_METRIC_FORECAST_TEMP_MAX ||
           metric == WEATHER_METRIC_FORECAST_RAIN_SUM ||
           metric == WEATHER_METRIC_FORECAST_RAIN_PROB_MAX;
}

static void formatMetricName(uint8_t metric, uint8_t windowHours, char* dst, size_t dstSize) {
    // Keep wording aligned with the UI labels in WebHandlers.cpp / WebPages.h so
    // simulation, status and configuration describe the same rule semantics.
    switch (metric) {
        case WEATHER_METRIC_CURRENT_TEMP:
            snprintf(dst, dstSize, "aktuelle Temperatur");
            break;
        case WEATHER_METRIC_FORECAST_TEMP_MAX:
            snprintf(dst, dstSize, "max. Temperatur in den nächsten %uh", windowHours);
            break;
        case WEATHER_METRIC_CURRENT_RAIN_MM:
            snprintf(dst, dstSize, "aktueller Niederschlag");
            break;
        case WEATHER_METRIC_CURRENT_RAIN_PROB:
            snprintf(dst, dstSize, "aktuelle Regenwahrscheinlichkeit");
            break;
        case WEATHER_METRIC_DAILY_RAIN_MM:
            snprintf(dst, dstSize, "Regen heute");
            break;
        case WEATHER_METRIC_DAILY_RAIN_PROB:
            snprintf(dst, dstSize, "Regenwahrscheinlichkeit heute");
            break;
        case WEATHER_METRIC_FORECAST_RAIN_SUM:
            snprintf(dst, dstSize, "Regen in den nächsten %uh", windowHours);
            break;
        case WEATHER_METRIC_FORECAST_RAIN_PROB_MAX:
            snprintf(dst, dstSize, "max. Regenwahrscheinlichkeit in den nächsten %uh", windowHours);
            break;
        default:
            snprintf(dst, dstSize, "Wetterwert");
            break;
    }
}

static void formatMetricValue(uint8_t metric, float value, char* dst, size_t dstSize) {
    switch (metric) {
        case WEATHER_METRIC_CURRENT_TEMP:
        case WEATHER_METRIC_FORECAST_TEMP_MAX:
            snprintf(dst, dstSize, "%.1f°C", value);
            break;
        case WEATHER_METRIC_CURRENT_RAIN_MM:
        case WEATHER_METRIC_DAILY_RAIN_MM:
        case WEATHER_METRIC_FORECAST_RAIN_SUM:
            snprintf(dst, dstSize, "%.1f mm", value);
            break;
        case WEATHER_METRIC_CURRENT_RAIN_PROB:
        case WEATHER_METRIC_DAILY_RAIN_PROB:
        case WEATHER_METRIC_FORECAST_RAIN_PROB_MAX:
            snprintf(dst, dstSize, "%.0f%%", value);
            break;
        default:
            snprintf(dst, dstSize, "%.1f", value);
            break;
    }
}

static const char* comparisonText(uint8_t comparison) {
    switch (comparison) {
        case WEATHER_OP_GT:  return ">";
        case WEATHER_OP_GTE: return ">=";
        case WEATHER_OP_LT:  return "<";
        case WEATHER_OP_LTE: return "<=";
        default:             return "?";
    }
}

static bool compareMetric(float actual, uint8_t comparison, float threshold) {
    switch (comparison) {
        case WEATHER_OP_GT:  return actual > threshold;
        case WEATHER_OP_GTE: return actual >= threshold;
        case WEATHER_OP_LT:  return actual < threshold;
        case WEATHER_OP_LTE: return actual <= threshold;
        default:
            Serial.printf("[Watering] Ungültiger Vergleichsoperator: %u\n", comparison);
            return false;
    }
}

static bool forecastWindowHasSamples(const WeatherData& w, time_t nowLocal, uint8_t windowHours,
                                     int* firstIndex, int* lastIndex) {
    if (firstIndex) *firstIndex = -1;
    if (lastIndex) *lastIndex = -1;
    if (w.hourlyCount == 0 || nowLocal <= 0) return false;

    time_t endTs = nowLocal + (time_t)max((int)windowHours, 1) * 3600;
    bool found = false;
    for (int i = 0; i < w.hourlyCount; i++) {
        time_t ts = w.hourlyTime[i];
        if (ts <= 0 || ts < nowLocal || ts > endTs) continue;
        if (firstIndex && *firstIndex < 0) *firstIndex = i;
        if (lastIndex) *lastIndex = i;
        found = true;
    }
    return found;
}

static bool resolveMetricValue(const WeatherRule& rule, const WeatherData& w, time_t nowLocal, float& outValue) {
    int first = -1;
    int last = -1;
    switch (rule.metric) {
        case WEATHER_METRIC_CURRENT_TEMP:
            outValue = w.temperature;
            return true;
        case WEATHER_METRIC_CURRENT_RAIN_MM:
            outValue = w.precipMm;
            return true;
        case WEATHER_METRIC_CURRENT_RAIN_PROB:
            outValue = w.precipProb;
            return true;
        case WEATHER_METRIC_DAILY_RAIN_MM:
            outValue = w.dailyPrecipMm;
            return true;
        case WEATHER_METRIC_DAILY_RAIN_PROB:
            outValue = w.dailyPrecipPct;
            return true;
        case WEATHER_METRIC_FORECAST_TEMP_MAX:
            if (forecastWindowHasSamples(w, nowLocal, rule.windowHours, &first, &last)) {
                float vmax = w.hourlyTemp[first];
                for (int i = first + 1; i <= last; i++) {
                    if (w.hourlyTime[i] <= 0) continue;
                    if (w.hourlyTemp[i] > vmax) vmax = w.hourlyTemp[i];
                }
                outValue = vmax;
                return true;
            }
            outValue = (rule.windowHours >= 24) ? w.tempMax : w.temperature;
            return true;
        case WEATHER_METRIC_FORECAST_RAIN_SUM:
            if (forecastWindowHasSamples(w, nowLocal, rule.windowHours, &first, &last)) {
                float sum = 0.0f;
                for (int i = first; i <= last; i++) {
                    if (w.hourlyTime[i] <= 0) continue;
                    sum += w.hourlyPrecipMm[i];
                }
                outValue = sum;
                return true;
            }
            outValue = (rule.windowHours >= 24) ? w.dailyPrecipMm : w.precipMm;
            return true;
        case WEATHER_METRIC_FORECAST_RAIN_PROB_MAX:
            if (forecastWindowHasSamples(w, nowLocal, rule.windowHours, &first, &last)) {
                float vmax = w.hourlyPrecipPct[first];
                for (int i = first + 1; i <= last; i++) {
                    if (w.hourlyTime[i] <= 0) continue;
                    if (w.hourlyPrecipPct[i] > vmax) vmax = w.hourlyPrecipPct[i];
                }
                outValue = vmax;
                return true;
            }
            outValue = (rule.windowHours >= 24) ? w.dailyPrecipPct : w.precipProb;
            return true;
        default:
            break;
    }
    return false;
}

static void fillRuleSummary(const WeatherRule& rule, float actualValue, char* dst, size_t dstSize) {
    char metricName[96];
    char actualBuf[24];
    char thresholdBuf[24];
    formatMetricName(rule.metric, metricUsesWindow(rule.metric) ? rule.windowHours : 24, metricName, sizeof(metricName));
    formatMetricValue(rule.metric, actualValue, actualBuf, sizeof(actualBuf));
    formatMetricValue(rule.metric, rule.threshold, thresholdBuf, sizeof(thresholdBuf));

    if (rule.actionType == WEATHER_RULE_SKIP) {
        snprintf(dst, dstSize, "Aussetzen: %s %s %s (Ist %s)",
                 metricName, comparisonText(rule.comparison), thresholdBuf, actualBuf);
    } else if (rule.actionType == WEATHER_RULE_REDUCE_RUNTIME) {
        snprintf(dst, dstSize, "Verkürzen um %u%%: %s %s %s (Ist %s)",
                 rule.effectPercent, metricName, comparisonText(rule.comparison), thresholdBuf, actualBuf);
    } else {
        snprintf(dst, dstSize, "Verlängern um %u%%: %s %s %s (Ist %s)",
                 rule.effectPercent, metricName, comparisonText(rule.comparison), thresholdBuf, actualBuf);
    }
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
                 "Wetter: %.1f°C, Regen heute %.1f mm, Regenwahrscheinlichkeit %.0f%%. Reihenfolge: Aussetzen zuerst, danach Zuschläge/Abzüge relativ zur Basislaufzeit.",
                 w.temperature, w.dailyPrecipMm, w.dailyPrecipPct);
        setText(out.weatherJustification, sizeof(out.weatherJustification), wb);
        if (input.weatherStale) {
            setText(out.warnings, sizeof(out.warnings), "Wetterdaten sind veraltet.");
        }
    }

    int runnableCount = 0;
    int reducedCount  = 0;
    int extendedCount = 0;
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
        p.adjustmentPercent = 0;
        p.action          = WATER_ACTION_EXECUTE;
        setText(p.reason, sizeof(p.reason), "Wird ausgeführt.");
        setText(p.policySource, sizeof(p.policySource), "Kein Wettertemplate");
        p.appliedRules[0] = '\0';

        if (!input.hardwareConfig->pumps[p.pumpIndex].enabled) {
            p.action = WATER_ACTION_SKIP;
            p.durationSec = 0;
            setText(p.reason, sizeof(p.reason), "Pumpe ist deaktiviert.");
            skippedCount++;
            continue;
        }

        WeatherTemplate effectiveTemplate;
        effectiveTemplate = WeatherTemplate{};
        if (asgn.weatherTemplateIndex >= 0 &&
            asgn.weatherTemplateIndex < input.slotConfig->weatherTemplateCount) {
            effectiveTemplate = input.slotConfig->weatherTemplates[asgn.weatherTemplateIndex];
            if (effectiveTemplate.name[0]) {
                snprintf(p.policySource, sizeof(p.policySource), "Template: %s", effectiveTemplate.name);
            } else {
                setText(p.policySource, sizeof(p.policySource), "Template");
            }
        } else if (asgn.useOwnWeatherPolicy) {
            appendLegacyPolicyRules(effectiveTemplate, asgn.weather);
            setText(p.policySource, sizeof(p.policySource), "Zuweisung (Legacy)");
        } else {
            WeatherPolicy legacyPolicy;
            legacyPolicy.skipIfRainMm = slot.skipIfRainMm;
            legacyPolicy.skipIfRainPct = slot.skipIfRainPct;
            legacyPolicy.runOnlyAboveTemp = slot.runOnlyAboveTemp;
            legacyPolicy.reduceIfRainMm = slot.reduceIfRainMm;
            legacyPolicy.reducePct = slot.reducePct;
            if (weatherPolicyIsActive(legacyPolicy)) {
                appendLegacyPolicyRules(effectiveTemplate, legacyPolicy);
                setText(p.policySource, sizeof(p.policySource), "Slot (Legacy)");
            }
        }

        if (input.weatherAvailable && input.weatherData && weatherTemplateHasRules(effectiveTemplate)) {
            const WeatherData& w = *input.weatherData;
            bool skipMatched = false;
            int  adjustmentDelta = 0;
            int  matchedSkipCount = 0;
            int  matchedAdjustCount = 0;

            for (int ri = 0; ri < effectiveTemplate.ruleCount; ri++) {
                const WeatherRule& rule = effectiveTemplate.rules[ri];
                if (!weatherRuleIsActive(rule) || rule.actionType != WEATHER_RULE_SKIP) continue;

                float actualValue = 0.0f;
                if (!resolveMetricValue(rule, w, input.nowLocal, actualValue)) continue;
                if (!compareMetric(actualValue, rule.comparison, rule.threshold)) continue;

                skipMatched = true;
                matchedSkipCount++;

                char summary[128];
                fillRuleSummary(rule, actualValue, summary, sizeof(summary));
                appendText(p.appliedRules, sizeof(p.appliedRules), summary);
            }

            if (skipMatched) {
                p.action = WATER_ACTION_SKIP;
                p.durationSec = 0;
                if (matchedSkipCount == 1) {
                    setText(p.reason, sizeof(p.reason), "Ausgesetzt durch Wetterregel.");
                } else {
                    snprintf(p.reason, sizeof(p.reason), "Ausgesetzt durch %d Wetterregeln.", matchedSkipCount);
                }
                skippedCount++;
                continue;
            }

            for (int ri = 0; ri < effectiveTemplate.ruleCount; ri++) {
                const WeatherRule& rule = effectiveTemplate.rules[ri];
                if (!weatherRuleIsActive(rule) || rule.actionType == WEATHER_RULE_SKIP) continue;

                float actualValue = 0.0f;
                if (!resolveMetricValue(rule, w, input.nowLocal, actualValue)) continue;
                if (!compareMetric(actualValue, rule.comparison, rule.threshold)) continue;

                matchedAdjustCount++;
                adjustmentDelta += (rule.actionType == WEATHER_RULE_REDUCE_RUNTIME)
                                       ? -(int)rule.effectPercent
                                       : (int)rule.effectPercent;

                char summary[128];
                fillRuleSummary(rule, actualValue, summary, sizeof(summary));
                appendText(p.appliedRules, sizeof(p.appliedRules), summary);
            }

            if (matchedAdjustCount > 0) {
                if (adjustmentDelta < -99) adjustmentDelta = -99;
                p.adjustmentPercent = adjustmentDelta;
                long adjusted = (long)p.baseDurationSec * (long)(100 + adjustmentDelta) / 100L;
                if (adjusted < 1) adjusted = 1;

                int pumpMaxRuntime = input.hardwareConfig->pumps[p.pumpIndex].maxRuntimeSec;
                if (pumpMaxRuntime > 0 && adjusted > pumpMaxRuntime) {
                    adjusted = pumpMaxRuntime;
                    appendText(p.appliedRules, sizeof(p.appliedRules), "Auf Pumpen-Maximalzeit begrenzt");
                }

                p.durationSec = (int)adjusted;
                if (p.durationSec < p.baseDurationSec) {
                    p.action = WATER_ACTION_REDUCE;
                    reducedCount++;
                    snprintf(p.reason, sizeof(p.reason),
                             "Laufzeit um %d%% reduziert (%ds statt %ds).",
                             -adjustmentDelta, p.durationSec, p.baseDurationSec);
                } else if (p.durationSec > p.baseDurationSec) {
                    p.action = WATER_ACTION_EXTEND;
                    extendedCount++;
                    snprintf(p.reason, sizeof(p.reason),
                             "Laufzeit um %+d%% verlängert (%ds statt %ds).",
                             adjustmentDelta, p.durationSec, p.baseDurationSec);
                } else {
                    p.action = WATER_ACTION_EXECUTE;
                    setText(p.reason, sizeof(p.reason),
                            "Wetterregeln getroffen, Ergebnis bleibt bei der Basislaufzeit.");
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
    } else if (reducedCount > 0 && extendedCount == 0) {
        setText(out.reason, sizeof(out.reason), "Slot wird mit reduzierter Laufzeit ausgeführt.");
        out.action = WATER_ACTION_REDUCE;
    } else if (extendedCount > 0 && reducedCount == 0) {
        setText(out.reason, sizeof(out.reason), "Slot wird mit verlängerter Laufzeit ausgeführt.");
        out.action = WATER_ACTION_EXTEND;
    } else if (reducedCount > 0 || extendedCount > 0) {
        setText(out.reason, sizeof(out.reason), "Slot wird mit kombinierten Wetteranpassungen ausgeführt.");
        out.action = WATER_ACTION_EXECUTE;
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
