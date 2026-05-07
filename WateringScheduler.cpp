#include "WateringScheduler.h"
#include <time.h>

WateringScheduler::WateringScheduler() {}

void WateringScheduler::begin(ConfigManager* cfg, RelayManager* rm, WeatherManager* wm) {
    _cfg = cfg;
    _rm  = rm;
    _wm  = wm;
    _qHead = _qTail = 0;
    _pumpRunning  = false;
    _activePump   = -1;
    _pumpOffAt    = 0;
    _lastCheckedMinute = 0;
}

// ─── Public API ───────────────────────────────────────────────────────────────

bool WateringScheduler::isBusy() const {
    return _pumpRunning || (_qHead != _qTail);
}

void WateringScheduler::update() {
    if (!_cfg || !_rm) return;

    bool armed = _cfg->isWateringConfigValid();

    // ── Step 1: advance running pump ─────────────────────────────────────────
    if (_pumpRunning) {
        // Millis wraparound-safe comparison: same pattern used in RelayManager
        static const unsigned long HALF = 0x80000000UL;
        if (_pumpOffAt != 0 && (millis() - _pumpOffAt) < HALF) {
            _rm->deactivateRelay(_activePump);
            Serial.printf("[Sched] Pump %d done.\n", _activePump);
            _pumpRunning = false;
            _activePump  = -1;
            _pumpOffAt   = 0;
        }
    }

    // ── Step 2: start next item from queue ────────────────────────────────────
    if (!_pumpRunning && _qHead != _qTail) {
        const QueueItem& item = _queue[_qHead % SCHEDULER_QUEUE_SIZE];
        _qHead = (_qHead + 1) % SCHEDULER_QUEUE_SIZE;

        if (armed) {
            if (_rm->activateRelay(item.pumpIndex, armed)) {
                _pumpRunning = true;
                _activePump  = item.pumpIndex;
                _pumpOffAt   = millis() + (unsigned long)item.durationSec * 1000UL;
                Serial.printf("[Sched] Pump %d ON for %ds (slot %d).\n",
                              item.pumpIndex, item.durationSec, item.slotIndex);
            } else {
                Serial.printf("[Sched] Pump %d could not be activated – skipped.\n",
                              item.pumpIndex);
            }
        } else {
            Serial.println("[Sched] Queue item dropped – watering not armed.");
        }
    }

    // ── Step 3: check for newly triggered slots (once per minute) ────────────
    time_t now = time(nullptr);
    if (now < 1000000L) return;  // system clock not yet set

    struct tm lt;
    localtime_r(&now, &lt);
    // Truncate to start of minute for reliable per-minute check
    time_t nowMinute = now - lt.tm_sec;
    if (nowMinute == _lastCheckedMinute) return;
    _lastCheckedMinute = nowMinute;

    // Pre-fetch weather data shortly before each slot fires – the WeatherManager
    // update() handles the actual HTTP fetch on its own schedule; we just ask it
    // here so it can request a refresh if its data is due.
    if (_wm) _wm->update();

    SlotConfig& sc = _cfg->getSlotConfig();
    for (int si = 0; si < sc.slotCount; si++) {
        const WateringSlot& slot = sc.slots[si];
        if (!slot.enabled) continue;

        // Check day-of-week (0=Mon … 6=Sun, matching our bitmask)
        int dow = (lt.tm_wday + 6) % 7;  // Sunday=0 in tm → Sunday=6 in our bitmask
        if (!(slot.days & (1 << dow))) continue;

        // Compute the expected trigger time for today
        time_t trigTime = computeTriggerTime(slot, now);
        if (trigTime == 0) continue;

        struct tm trigTm;
        localtime_r(&trigTime, &trigTm);

        // Fire if the current minute matches the trigger minute
        if (lt.tm_hour != trigTm.tm_hour || lt.tm_min != trigTm.tm_min) continue;

        // Weather check
        if (!shouldRunSlot(slot)) {
            Serial.printf("[Sched] Slot '%s' skipped by weather rule.\n", slot.name);
            continue;
        }

        enqueueSlot(si);
    }
}

// ─── Private helpers ──────────────────────────────────────────────────────────

time_t WateringScheduler::computeTriggerTime(const WateringSlot& slot, time_t localNow) const {
    // For FIXED_TIME: simply build today's local epoch at (fixedHour, fixedMinute)
    auto buildFixed = [&]() -> time_t {
        struct tm lt;
        localtime_r(&localNow, &lt);
        lt.tm_hour = slot.fixedHour;
        lt.tm_min  = slot.fixedMinute;
        lt.tm_sec  = 0;
        lt.tm_isdst = -1;
        time_t t = mktime(&lt);
        return (t == (time_t)-1) ? 0 : t;
    };

    if (slot.triggerType == TRIGGER_FIXED_TIME) {
        return buildFixed();
    }

    // Astronomical triggers require weather data for sunrise/sunset
    bool haveAstro = _wm && _wm->isAvailable() &&
                     _wm->getSunrise() != 0 && _wm->getSunset() != 0;

    if (!haveAstro) {
        // No astronomical data – fall back to the fixed clock time
        return buildFixed();
    }

    time_t base = 0;
    switch (slot.triggerType) {
        case TRIGGER_SUNRISE: base = _wm->getSunrise(); break;
        case TRIGGER_SUNSET:  base = _wm->getSunset();  break;
        case TRIGGER_MIDDAY:  base = _wm->getMidday();  break;
        case TRIGGER_OFFSET:
            switch (slot.offsetBase) {
                case OFFSET_BASE_SUNRISE: base = _wm->getSunrise(); break;
                case OFFSET_BASE_SUNSET:  base = _wm->getSunset();  break;
                case OFFSET_BASE_MIDDAY:  base = _wm->getMidday();  break;
                default:                  base = _wm->getSunrise();  break;
            }
            return (base != 0) ? base + (time_t)slot.offsetMinutes * 60 : buildFixed();
        default:
            return buildFixed();
    }
    return (base != 0) ? base : buildFixed();
}

bool WateringScheduler::shouldRunSlot(const WateringSlot& slot) const {
    if (!_wm || !_wm->isAvailable()) return true;  // no data → always run

    const WeatherData& w = _wm->getData();

    // Skip if daily expected precipitation exceeds threshold
    if (slot.skipIfRainMm > 0.0f && w.dailyPrecipMm >= slot.skipIfRainMm) {
        Serial.printf("[Sched] Rain %.1f mm >= skip threshold %.1f mm.\n",
                      w.dailyPrecipMm, slot.skipIfRainMm);
        return false;
    }
    // Skip if precipitation probability exceeds threshold
    if (slot.skipIfRainPct > 0.0f && w.dailyPrecipPct >= slot.skipIfRainPct) {
        Serial.printf("[Sched] Rain prob %.0f%% >= skip threshold %.0f%%.\n",
                      w.dailyPrecipPct, slot.skipIfRainPct);
        return false;
    }
    // Skip if temperature is below the required minimum
    if (slot.runOnlyAboveTemp > -99.0f && w.temperature < slot.runOnlyAboveTemp) {
        Serial.printf("[Sched] Temp %.1f°C < required %.1f°C.\n",
                      w.temperature, slot.runOnlyAboveTemp);
        return false;
    }
    return true;
}

int WateringScheduler::computeDuration(const WateringSlot& slot, int baseDuration) const {
    if (!_wm || !_wm->isAvailable()) return baseDuration;
    if (slot.reduceIfRainMm <= 0.0f) return baseDuration;

    const WeatherData& w = _wm->getData();
    if (w.dailyPrecipMm >= slot.reduceIfRainMm) {
        int reduced = baseDuration * (100 - (int)slot.reducePct) / 100;
        if (reduced < 1) reduced = 1;
        Serial.printf("[Sched] Rain %.1f mm >= %.1f mm – duration %d→%ds (-%d%%).\n",
                      w.dailyPrecipMm, slot.reduceIfRainMm,
                      baseDuration, reduced, slot.reducePct);
        return reduced;
    }
    return baseDuration;
}

void WateringScheduler::enqueueSlot(int slotIdx) {
    if (!_cfg) return;
    SlotConfig& sc     = _cfg->getSlotConfig();
    HardwareConfig& hw = _cfg->getHardwareConfig();
    const WateringSlot& slot = sc.slots[slotIdx];

    int added = 0;
    for (int ai = 0; ai < sc.assignCount; ai++) {
        const SlotPumpAssignment& asgn = sc.assignments[ai];
        if (asgn.slotIndex != (uint8_t)slotIdx) continue;
        if (asgn.pumpIndex >= (uint8_t)hw.relayCount) continue;

        int duration = computeDuration(slot, asgn.durationSec);

        // Check queue capacity (circular buffer)
        int nextTail = (_qTail + 1) % SCHEDULER_QUEUE_SIZE;
        if (nextTail == _qHead) {
            Serial.printf("[Sched] Queue full – pump %d for slot '%s' dropped.\n",
                          asgn.pumpIndex, slot.name);
            continue;
        }
        _queue[_qTail].pumpIndex   = asgn.pumpIndex;
        _queue[_qTail].durationSec = duration;
        _queue[_qTail].slotIndex   = (uint8_t)slotIdx;
        _qTail = nextTail;
        added++;
    }
    if (added > 0) {
        Serial.printf("[Sched] Slot '%s' triggered – %d pump(s) queued.\n",
                      slot.name, added);
    }
}
