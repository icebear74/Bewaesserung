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
        // If watchdog or manual action already shut this pump down, clear scheduler state.
        if (!_rm->isActive(_activePump)) {
            Serial.printf("[Sched] Pump %d no longer active (stopped externally).\n", _activePump);
            _pumpRunning = false;
            _activePump  = -1;
            _pumpOffAt   = 0;
        }

        // Millis wraparound-safe deadline check: after the deadline, millis() - target
        // wraps to a large positive value >= 2^31; before the deadline the difference
        // is small (< 2^31).  Subtract unsigned and compare to MILLIS_HALF_RANGE.
        static const unsigned long MILLIS_HALF_RANGE = 0x80000000UL;
        if (_pumpOffAt != 0 && (millis() - _pumpOffAt) < MILLIS_HALF_RANGE) {
            _rm->deactivateRelay(_activePump, "slot runtime complete");
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
            if (_rm->activateRelay(item.pumpIndex, armed, item.slotIndex, item.durationSec)) {
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
    HardwareConfig& hw = _cfg->getHardwareConfig();
    const WeatherData* weatherData = (_wm && _wm->isAvailable()) ? &_wm->getData() : nullptr;
    bool weatherAvailable = (_wm && _wm->isAvailable());
    bool weatherStale = (_wm && _wm->isStale());
    for (int si = 0; si < sc.slotCount; si++) {
        WateringDecisionInput in;
        in.slotConfig = &sc;
        in.hardwareConfig = &hw;
        in.weatherData = weatherData;
        in.weatherAvailable = weatherAvailable;
        in.weatherStale = weatherStale;
        in.nowLocal = now;
        in.slotIndex = si;
        in.enforceDayMatch = true;
        in.enforceTriggerMinute = true;

        WateringDecisionResult decision = WateringDecisionEngine::evaluateSlot(in);
        if (decision.action == WATER_ACTION_SKIP) {
            if (decision.triggerMatched) {
                Serial.printf("[Sched] Slot '%s' skipped: %s\n",
                              sc.slots[si].name, decision.reason);
            }
            continue;
        }
        enqueueDecision(decision);
    }
}

// ─── Private helpers ──────────────────────────────────────────────────────────

void WateringScheduler::enqueueDecision(const WateringDecisionResult& decision) {
    if (!_cfg || decision.slotIndex < 0 || decision.slotIndex >= _cfg->getSlotConfig().slotCount) return;
    SlotConfig& sc = _cfg->getSlotConfig();
    const WateringSlot& slot = sc.slots[decision.slotIndex];
    int added = 0;
    for (int i = 0; i < decision.planCount; i++) {
        const WateringDecisionPumpPlan& pp = decision.plan[i];
        // Check queue capacity (circular buffer)
        int nextTail = (_qTail + 1) % SCHEDULER_QUEUE_SIZE;
        if (nextTail == _qHead) {
            Serial.printf("[Sched] Queue full – pump %d for slot '%s' dropped.\n",
                          pp.pumpIndex, slot.name);
            continue;
        }
        _queue[_qTail].pumpIndex   = pp.pumpIndex;
        _queue[_qTail].durationSec = pp.durationSec;
        _queue[_qTail].slotIndex   = (uint8_t)decision.slotIndex;
        _qTail = nextTail;
        added++;
    }
    if (added > 0) {
        Serial.printf("[Sched] Slot '%s' queued – %d pump(s), action=%d, reason=%s\n",
                      slot.name, added, (int)decision.action, decision.reason);
    }
}
