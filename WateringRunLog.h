#pragma once
#include <Arduino.h>
#include <time.h>

// ─── Configuration ─────────────────────────────────────────────────────────────

// Path to the run-log JSON file on LittleFS
#define RUNLOG_FILE            "/runlog.json"

// Hard cap on stored entries (newest-first). At ~100 bytes/entry this is ~50 KB.
#define RUNLOG_MAX_ENTRIES     500

// Minimum free LittleFS space required before writing a new entry.
// Prevents runlog from filling the filesystem.
#define RUNLOG_MIN_FREE_BYTES  65536UL   // 64 KB

// Path to the decision-log JSON file on LittleFS
#define DECISIONLOG_FILE       "/decisionlog.json"

// Hard cap on stored decision log entries (newest-first).
#define DECISIONLOG_MAX_ENTRIES 200

// ─── WateringRunLog ────────────────────────────────────────────────────────────

/**
 * Lightweight append-only log of pump activation events.
 *
 * Entries are stored newest-first so the file is ready to serve directly.
 * Each entry: {"t":<epoch>,"sn":"slotName","pn":"pumpName","dur":<sec>}
 *
 * The log file is trimmed to RUNLOG_MAX_ENTRIES on every append and writing is
 * skipped entirely when LittleFS has less than RUNLOG_MIN_FREE_BYTES free.
 */
class WateringRunLog {
public:
    WateringRunLog() = default;

    /**
     * Append one pump-activation event to the log.
     * May block for up to ~1 s on the first call after the log is full while
     * the file is rewritten.  Activations are rare (at most a few per day) so
     * this is acceptable.
     */
    void append(time_t ts, const char* slotName, const char* pumpName, int durationSec);

    /**
     * Serialise the full log (newest first) into @p out as a JSON array.
     * Returns false only if the file exists but cannot be opened.
     */
    bool getJson(String& out) const;

    /** Delete the log file. */
    void clear();

    /**
     * Append a watering decision event (including skips) to the decision log.
     * Stored in a separate ring-buffer file /decisionlog.json.
     */
    void appendDecision(time_t ts, const char* slotName, const char* action,
                        const char* reason, int durationSec = 0);

    /**
     * Serialise the decision log (newest first) into @p out as a JSON array.
     * Returns false only if the file exists but cannot be opened.
     */
    bool getDecisionLogJson(String& out) const;

    /** Delete the decision log file. */
    void clearDecisionLog();
};
