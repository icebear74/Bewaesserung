#include "WateringRunLog.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

// ─── PSRAM-backed allocator for ArduinoJson ────────────────────────────────────

struct RunLogPsramAllocator : ArduinoJson::Allocator {
    void* allocate(size_t size) override {
        void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return p ? p : malloc(size);
    }
    void deallocate(void* pointer) override { free(pointer); }
    void* reallocate(void* ptr, size_t new_size) override {
        void* p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return p ? p : realloc(ptr, new_size);
    }
};

// ─── WateringRunLog implementation ────────────────────────────────────────────

void WateringRunLog::append(time_t ts, const char* slotName, const char* pumpName, int durationSec) {
    // Guard: skip if there is not enough free space on LittleFS
    size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
    if (freeBytes < RUNLOG_MIN_FREE_BYTES) {
        Serial.printf("[RunLog] Wenig Speicher (%u B frei), Eintrag wird übersprungen.\n", (unsigned)freeBytes);
        return;
    }

    RunLogPsramAllocator alloc;

    // ── Load existing entries ──────────────────────────────────────────────────
    JsonDocument existing(&alloc);
    bool hasExisting = false;
    if (LittleFS.exists(RUNLOG_FILE)) {
        File f = LittleFS.open(RUNLOG_FILE, "r");
        if (f) {
            DeserializationError err = deserializeJson(existing, f);
            f.close();
            hasExisting = (!err && existing.is<JsonArray>());
        }
    }

    // ── Build new document: new entry first, then old ones ────────────────────
    JsonDocument out_doc(&alloc);
    JsonArray out_arr = out_doc.to<JsonArray>();

    // Prepend new entry
    JsonObject entry = out_arr.add<JsonObject>();
    entry["t"]   = (long long)ts;
    entry["sn"]  = slotName  ? slotName  : "";
    entry["pn"]  = pumpName  ? pumpName  : "";
    entry["dur"] = durationSec;

    // Copy old entries (up to MAX-1 so total stays ≤ MAX)
    if (hasExisting) {
        int remaining = RUNLOG_MAX_ENTRIES - 1;
        for (JsonVariant v : existing.as<JsonArray>()) {
            if (remaining-- <= 0) break;
            out_arr.add(v);
        }
    }

    // ── Persist ───────────────────────────────────────────────────────────────
    File fw = LittleFS.open(RUNLOG_FILE, "w");
    if (fw) {
        serializeJson(out_doc, fw);
        fw.close();
        Serial.printf("[RunLog] Eintrag gespeichert: %s / %s / %ds\n",
                      slotName ? slotName : "?", pumpName ? pumpName : "?", durationSec);
    } else {
        Serial.println("[RunLog] FEHLER: Datei konnte nicht geöffnet werden.");
    }
}

bool WateringRunLog::getJson(String& out) const {
    if (!LittleFS.exists(RUNLOG_FILE)) {
        out = "[]";
        return true;
    }
    File f = LittleFS.open(RUNLOG_FILE, "r");
    if (!f) {
        out = "[]";
        return false;
    }
    out = "";
    out.reserve(f.size() + 4);
    while (f.available()) out += (char)f.read();
    f.close();
    return true;
}

void WateringRunLog::clear() {
    LittleFS.remove(RUNLOG_FILE);
    Serial.println("[RunLog] Protokoll gelöscht.");
}

// ─── Decision log implementation ──────────────────────────────────────────────

void WateringRunLog::appendDecision(time_t ts, const char* slotName, const char* action,
                                    const char* reason, int durationSec) {
    size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
    if (freeBytes < RUNLOG_MIN_FREE_BYTES) return;

    RunLogPsramAllocator alloc;

    JsonDocument existing(&alloc);
    bool hasExisting = false;
    if (LittleFS.exists(DECISIONLOG_FILE)) {
        File f = LittleFS.open(DECISIONLOG_FILE, "r");
        if (f) {
            DeserializationError err = deserializeJson(existing, f);
            f.close();
            hasExisting = (!err && existing.is<JsonArray>());
        }
    }

    JsonDocument out_doc(&alloc);
    JsonArray out_arr = out_doc.to<JsonArray>();

    JsonObject entry = out_arr.add<JsonObject>();
    entry["t"]      = (long long)ts;
    entry["sn"]     = slotName ? slotName : "";
    entry["action"] = action   ? action   : "skip";
    entry["reason"] = reason   ? reason   : "";
    entry["dur"]    = durationSec;

    if (hasExisting) {
        int remaining = DECISIONLOG_MAX_ENTRIES - 1;
        for (JsonVariant v : existing.as<JsonArray>()) {
            if (remaining-- <= 0) break;
            out_arr.add(v);
        }
    }

    File fw = LittleFS.open(DECISIONLOG_FILE, "w");
    if (fw) {
        serializeJson(out_doc, fw);
        fw.close();
    }
}

bool WateringRunLog::getDecisionLogJson(String& out) const {
    if (!LittleFS.exists(DECISIONLOG_FILE)) {
        out = "[]";
        return true;
    }
    File f = LittleFS.open(DECISIONLOG_FILE, "r");
    if (!f) {
        out = "[]";
        return false;
    }
    out = "";
    out.reserve(f.size() + 4);
    while (f.available()) out += (char)f.read();
    f.close();
    return true;
}

void WateringRunLog::clearDecisionLog() {
    LittleFS.remove(DECISIONLOG_FILE);
    Serial.println("[RunLog] Entscheidungsprotokoll gelöscht.");
}
