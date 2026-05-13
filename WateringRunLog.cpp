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

void WateringRunLog::fillLastStartTimes(const char* const* pumpNames, time_t* results, int count) const {
    for (int i = 0; i < count; i++) results[i] = 0;
    if (count <= 0 || count > MAX_RELAY_COUNT || !LittleFS.exists(RUNLOG_FILE)) return;

    File f = LittleFS.open(RUNLOG_FILE, "r");
    if (!f) return;

    RunLogPsramAllocator alloc;
    JsonDocument doc(&alloc);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err || !doc.is<JsonArray>()) return;

    bool found[MAX_RELAY_COUNT] = {};
    int remaining = count;
    for (JsonVariant v : doc.as<JsonArray>()) {
        if (!v.is<JsonObject>() || remaining == 0) break;
        const char* pn = v["pn"].as<const char*>();
        if (!pn || pn[0] == '\0') continue;
        for (int i = 0; i < count; i++) {
            if (found[i] || !pumpNames[i] || pumpNames[i][0] == '\0') continue;
            if (strcmp(pn, pumpNames[i]) == 0) {
                results[i] = (time_t)v["t"].as<long long>();
                found[i] = true;
                remaining--;
                break;
            }
        }
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
