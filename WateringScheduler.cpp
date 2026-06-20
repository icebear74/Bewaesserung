#include "WateringScheduler.h"
#include <time.h>

WateringScheduler::WateringScheduler() {
    // Allocate the decision result buffer in PSRAM to keep the ~14 KB struct
    // off the loopTask stack (default 8 KB).
    _decisionBuf = static_cast<WateringDecisionResult*>(
        heap_caps_calloc(1, sizeof(WateringDecisionResult), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!_decisionBuf) {
        // Fallback to internal heap if PSRAM is unavailable
        _decisionBuf = static_cast<WateringDecisionResult*>(calloc(1, sizeof(WateringDecisionResult)));
    }
    if (!_decisionBuf) {
        Serial.println("[Sched] FATAL: failed to allocate decision buffer.");
    }
}

WateringScheduler::~WateringScheduler() {
    free(_decisionBuf);
    _decisionBuf = nullptr;
}

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

    time_t now = time(nullptr);
    bool armed = _cfg->isWateringConfigValid();
    bool automationLocked = _cfg->isAutomationLocked(now);

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

        if (automationLocked) {
            Serial.printf("[Sched] Queue item for pump %d dropped – Automatiksperre aktiv.\n", item.pumpIndex);
        } else if (armed) {
            if (_rm->activateRelay(item.pumpIndex, armed, item.slotIndex, item.durationSec)) {
                _pumpRunning = true;
                _activePump  = item.pumpIndex;
                _pumpOffAt   = millis() + (unsigned long)item.durationSec * 1000UL;
                Serial.printf("[Sched] Pump %d ON for %ds (slot %d).\n",
                              item.pumpIndex, item.durationSec, item.slotIndex);
                // Log the activation to the rotating run log
                if (_runLog) {
                    SlotConfig&    sc = _cfg->getSlotConfig();
                    HardwareConfig& hw = _cfg->getHardwareConfig();
                    const char* slotName = (item.slotIndex < sc.slotCount)
                                          ? sc.slots[item.slotIndex].name : "?";
                    const char* pumpName = (item.pumpIndex < hw.relayCount)
                                          ? hw.pumps[item.pumpIndex].name : "?";
                    _runLog->append(time(nullptr), slotName, pumpName, item.durationSec);
                }
            } else {
                Serial.printf("[Sched] Pump %d could not be activated – skipped.\n",
                              item.pumpIndex);
            }
        } else {
            Serial.println("[Sched] Queue item dropped – watering not armed.");
        }
    }

    // ── Step 3: check for newly triggered slots (once per minute) ────────────
    if (now < 1000000L) return;  // system clock not yet set

    struct tm lt;
    localtime_r(&now, &lt);
    // Truncate to start of minute for reliable per-minute check
    time_t nowMinute = now - lt.tm_sec;
    if (nowMinute == _lastCheckedMinute) return;
    _lastCheckedMinute = nowMinute;

    // Auto-expire stale locks and persist if any changed
    if (_cfg->expireLocks(now)) {
        _cfg->saveSlotConfig();
    }

    SlotConfig& sc = _cfg->getSlotConfig();
    HardwareConfig& hw = _cfg->getHardwareConfig();
    // Preflight weather refresh: if a slot triggers in ~1 minute, request update
    // before evaluating the execution decision.
    if (_wm) {
        bool preflightNeeded = false;
        time_t inOneMinute = now + 60;
        struct tm inOneTm;
        localtime_r(&inOneMinute, &inOneTm);
        for (int si = 0; si < sc.slotCount && !preflightNeeded; si++) {
            bool usedFallback = false;
            time_t t = WateringDecisionEngine::computeTriggerTime(
                sc.slots[si], inOneMinute, _wm->isAvailable() ? &_wm->getData() : nullptr,
                _wm->isAvailable(), &usedFallback);
            if (t <= 0) continue;
            struct tm tt;
            localtime_r(&t, &tt);
            if (tt.tm_hour == inOneTm.tm_hour && tt.tm_min == inOneTm.tm_min) {
                preflightNeeded = true;
            }
        }
        if (preflightNeeded) _wm->requestRefresh();
        _wm->update();
    }
    const WeatherData* weatherData = (_wm && _wm->isAvailable()) ? &_wm->getData() : nullptr;
    bool weatherAvailable = (_wm && _wm->isAvailable());
    bool weatherStale = (_wm && _wm->isStale());
    if (automationLocked) {
        // Drop stale queued auto-runs while lock is active.
        if (_qHead != _qTail) {
            _qHead = _qTail = 0;
            Serial.println("[Sched] Automatiksperre aktiv – Warteschlange geleert.");
        }
        return;
    }

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

        if (!_decisionBuf) continue;
        WateringDecisionEngine::evaluateSlot(in, *_decisionBuf);
        if (_decisionBuf->action == WATER_ACTION_SKIP) {
            if (_decisionBuf->triggerMatched) {
                Serial.printf("[Sched] Slot '%s' skipped: %s\n",
                              sc.slots[si].name, _decisionBuf->reason);
                // Log skip to decision history
                if (_runLog) {
                    _runLog->appendDecision(now, sc.slots[si].name, "skip",
                                            _decisionBuf->reason, 0);
                }
            }
            continue;
        }
        enqueueDecision(*_decisionBuf);
        // Log the decision to history
        if (_runLog) {
            const char* actionStr = "execute";
            if (_decisionBuf->action == WATER_ACTION_REDUCE) actionStr = "reduce";
            else if (_decisionBuf->action == WATER_ACTION_EXTEND) actionStr = "extend";
            else if (_decisionBuf->action == WATER_ACTION_FALLBACK) actionStr = "fallback";
            _runLog->appendDecision(now, sc.slots[si].name, actionStr,
                                    _decisionBuf->reason, _decisionBuf->totalDurationSec);
        }
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
        if (pp.action == WATER_ACTION_SKIP || pp.durationSec <= 0) {
            Serial.printf("[Sched] Pump %d for slot '%s' skipped by decision: %s\n",
                          pp.pumpIndex, slot.name, pp.reason);
            continue;
        }
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
