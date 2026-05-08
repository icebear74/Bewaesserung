#include "ConfigManager.h"
#include <LittleFS.h>

static bool weatherPolicyIsActive(const WeatherPolicy& policy) {
    return policy.skipIfRainMm > 0.0f ||
           policy.skipIfRainPct > 0.0f ||
           policy.runOnlyAboveTemp > -99.0f ||
           policy.reduceIfRainMm > 0.0f;
}

static WeatherPolicy makeSlotLegacyPolicy(const WateringSlot& slot) {
    WeatherPolicy policy;
    policy.skipIfRainMm = slot.skipIfRainMm;
    policy.skipIfRainPct = slot.skipIfRainPct;
    policy.runOnlyAboveTemp = slot.runOnlyAboveTemp;
    policy.reduceIfRainMm = slot.reduceIfRainMm;
    policy.reducePct = slot.reducePct;
    return policy;
}

static bool weatherPolicyEquals(const WeatherPolicy& a, const WeatherPolicy& b) {
    return a.skipIfRainMm == b.skipIfRainMm &&
           a.skipIfRainPct == b.skipIfRainPct &&
           a.runOnlyAboveTemp == b.runOnlyAboveTemp &&
           a.reduceIfRainMm == b.reduceIfRainMm &&
           a.reducePct == b.reducePct;
}

static void clearWeatherPolicy(WeatherPolicy& policy) {
    policy = WeatherPolicy{};
}

static void clearSlotLegacyWeather(WateringSlot& slot) {
    slot.skipIfRainMm = 0.0f;
    slot.skipIfRainPct = 0.0f;
    slot.runOnlyAboveTemp = -99.0f;
    slot.reduceIfRainMm = 0.0f;
    slot.reducePct = 50;
}

static int ensureWeatherTemplate(SlotConfig& sc, const char* preferredName, const WeatherPolicy& policy) {
    for (int i = 0; i < sc.weatherTemplateCount; i++) {
        if (weatherPolicyEquals(sc.weatherTemplates[i].weather, policy)) {
            return i;
        }
    }
    if (sc.weatherTemplateCount >= MAX_WEATHER_TEMPLATES) return -1;

    int idx = sc.weatherTemplateCount++;
    WeatherTemplate& wt = sc.weatherTemplates[idx];
    wt = WeatherTemplate{};
    wt.weather = policy;
    if (preferredName && preferredName[0]) {
        strlcpy(wt.name, preferredName, sizeof(wt.name));
    } else {
        snprintf(wt.name, sizeof(wt.name), "Wetter %d", idx + 1);
    }
    return idx;
}

ConfigManager::ConfigManager() {}

void ConfigManager::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("[Config] LittleFS mount failed, formatted.");
    } else {
        Serial.println("[Config] LittleFS mounted.");
    }
    loadDeviceConfig();
    loadHardwareConfig();
    loadSlotConfig();
}

// ─── Device Config ────────────────────────────────────────────────────────────

bool ConfigManager::loadDeviceConfig() {
    File f = LittleFS.open("/config.json", "r");
    if (!f) {
        Serial.println("[Config] /config.json not found, using defaults.");
        return false;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("[Config] /config.json parse error: %s\n", err.c_str());
        return false;
    }
    strlcpy(_deviceConfig.hostname,     doc["hostname"]     | "Bewaesserung",              sizeof(_deviceConfig.hostname));
    strlcpy(_deviceConfig.ssid,         doc["ssid"]         | "",                           sizeof(_deviceConfig.ssid));
    strlcpy(_deviceConfig.password,     doc["password"]     | "",                           sizeof(_deviceConfig.password));
    strlcpy(_deviceConfig.timezone,     doc["timezone"]     | "CET-1CEST,M3.5.0,M10.5.0/3",sizeof(_deviceConfig.timezone));
    strlcpy(_deviceConfig.ntpServer,    doc["ntpServer"]    | "pool.ntp.org",               sizeof(_deviceConfig.ntpServer));
    strlcpy(_deviceConfig.locationName, doc["locationName"] | "",                           sizeof(_deviceConfig.locationName));
    _deviceConfig.latitude  = doc["latitude"]  | 48.1351f;
    _deviceConfig.longitude = doc["longitude"] | 11.5820f;
    Serial.println("[Config] Device config loaded.");
    return true;
}

bool ConfigManager::saveDeviceConfig() {
    File f = LittleFS.open("/config.json", "w");
    if (!f) {
        Serial.println("[Config] Cannot write /config.json");
        return false;
    }
    JsonDocument doc;
    doc["hostname"]     = _deviceConfig.hostname;
    doc["ssid"]         = _deviceConfig.ssid;
    doc["password"]     = _deviceConfig.password;
    doc["timezone"]     = _deviceConfig.timezone;
    doc["ntpServer"]    = _deviceConfig.ntpServer;
    doc["latitude"]     = _deviceConfig.latitude;
    doc["longitude"]    = _deviceConfig.longitude;
    doc["locationName"] = _deviceConfig.locationName;
    serializeJson(doc, f);
    f.close();
    Serial.println("[Config] Device config saved.");
    return true;
}

// ─── Hardware Config ──────────────────────────────────────────────────────────

bool ConfigManager::loadHardwareConfig() {
    File f = LittleFS.open("/hardware.json", "r");
    if (!f) {
        Serial.println("[Config] /hardware.json not found, using defaults.");
        return false;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("[Config] /hardware.json parse error: %s\n", err.c_str());
        return false;
    }
    _hardwareConfig.relayCount    = constrain((int)(doc["relayCount"] | 0), 0, MAX_RELAY_COUNT);
    _hardwareConfig.relayInverted = doc["relayInverted"] | false;

    // ── Load optional hardware: expander chips ────────────────────────────────
    _hardwareConfig.expanderCount = 0;
    if (doc["expanders"].is<JsonArray>()) {
        JsonArray expArr = doc["expanders"].as<JsonArray>();
        int cnt = min((int)expArr.size(), MAX_EXPANDER_COUNT);
        _hardwareConfig.expanderCount = cnt;
        for (int i = 0; i < cnt; i++) {
            ExpanderEntry& e = _hardwareConfig.expanders[i];
            e = ExpanderEntry{};
            JsonObject eo = expArr[i].as<JsonObject>();
            e.enabled    = eo["enabled"]    | false;
            e.chipType   = eo["chipType"]   | (uint8_t)EXPANDER_TYPE_PCF8574;
            e.i2cAddress = eo["i2cAddress"] | (uint8_t)0x20;
            strlcpy(e.name, eo["name"] | "", sizeof(e.name));
        }
    }

    if (doc["pumps"].is<JsonArray>()) {
        // New format: per-pump entries
        JsonArray pumps = doc["pumps"].as<JsonArray>();
        for (int i = 0; i < MAX_RELAY_COUNT; i++) {
            PumpEntry& p = _hardwareConfig.pumps[i];
            p = PumpEntry{};  // reset to defaults
            if (i < (int)pumps.size()) {
                JsonObject po = pumps[i].as<JsonObject>();
                p.enabled       = po["enabled"]       | false;
                p.outputType    = (uint8_t)constrain((int)(po["outputType"] | (int)OUTPUT_TYPE_GPIO),
                                                     (int)OUTPUT_TYPE_GPIO, (int)OUTPUT_TYPE_PCF8574);
                p.pin           = po["pin"]            | -1;
                p.invertLogic   = po["invertLogic"]    | false;
                p.maxRuntimeSec = po["maxRuntimeSec"]  | 300;
                strlcpy(p.name,  po["name"]  | "", sizeof(p.name));
                strlcpy(p.notes, po["notes"] | "", sizeof(p.notes));
                if (p.outputType == OUTPUT_TYPE_PCF8574) {
                    if (po["expanderIndex"].is<int>()) {
                        // Current format: pump references expander by index
                        p.expanderIndex = po["expanderIndex"] | (uint8_t)0;
                        p.i2cChannel    = po["i2cChannel"]    | (uint8_t)0;
                    } else if (po["i2cAddress"].is<int>()) {
                        // Migration from old format (i2cAddress directly in pump):
                        // find or create an expander entry for this address.
                        uint8_t oldAddr  = po["i2cAddress"] | (uint8_t)0x20;
                        p.i2cChannel     = po["i2cChannel"] | (uint8_t)0;
                        p.expanderIndex  = 0;
                        bool found = false;
                        for (int d = 0; d < _hardwareConfig.expanderCount; d++) {
                            if (_hardwareConfig.expanders[d].i2cAddress == oldAddr) {
                                p.expanderIndex = (uint8_t)d;
                                found = true;
                                break;
                            }
                        }
                        if (!found && _hardwareConfig.expanderCount < MAX_EXPANDER_COUNT) {
                            int d = _hardwareConfig.expanderCount++;
                            ExpanderEntry& e = _hardwareConfig.expanders[d];
                            e = ExpanderEntry{};
                            e.enabled    = true;
                            e.chipType   = EXPANDER_TYPE_PCF8574;
                            e.i2cAddress = oldAddr;
                            char nameBuf[32];
                            snprintf(nameBuf, sizeof(nameBuf), "Expander %d", d + 1);
                            strlcpy(e.name, nameBuf, sizeof(e.name));
                            p.expanderIndex = (uint8_t)d;
                            Serial.printf("[Config] Migration: created expander entry %d for address 0x%02X (pump %d).\n",
                                          d + 1, oldAddr, i + 1);
                        }
                    }
                }
            }
        }
        Serial.printf("[Config] Hardware config loaded: %d pumps, %d expander(s).\n",
                      _hardwareConfig.relayCount, _hardwareConfig.expanderCount);
    } else if (doc["relayPins"].is<JsonArray>()) {
        // Old format: migrate relayPins[] to pumps[]
        JsonArray pins = doc["relayPins"].as<JsonArray>();
        for (int i = 0; i < MAX_RELAY_COUNT; i++) {
            PumpEntry& p = _hardwareConfig.pumps[i];
            p = PumpEntry{};
            int pin = (i < (int)pins.size()) ? (int)pins[i] : -1;
            p.pin         = pin;
            p.enabled     = (pin >= 0) && (i < _hardwareConfig.relayCount);
            p.invertLogic = _hardwareConfig.relayInverted;
            char nameBuf[32];
            snprintf(nameBuf, sizeof(nameBuf), "Pumpe %d", i + 1);
            strlcpy(p.name, nameBuf, sizeof(p.name));
        }
        Serial.printf("[Config] Hardware config loaded (legacy format, migrated): %d relays.\n", _hardwareConfig.relayCount);
    }

    // Sanitize per-pump expander/channel fields loaded from JSON so that runtime
    // never addresses invalid expander ports.
    for (int i = 0; i < MAX_RELAY_COUNT; i++) {
        PumpEntry& p = _hardwareConfig.pumps[i];
        if (p.outputType != OUTPUT_TYPE_PCF8574) {
            p.expanderIndex = 0;
            p.i2cChannel    = 0;
            continue;
        }

        int maxExpanderIndex = (_hardwareConfig.expanderCount > 0)
                                   ? (_hardwareConfig.expanderCount - 1)
                                   : 0;
        p.expanderIndex = (uint8_t)constrain((int)p.expanderIndex, 0, maxExpanderIndex);
        uint8_t maxChannel = 7;  // PCF8574 default
        if (p.expanderIndex < _hardwareConfig.expanderCount &&
            _hardwareConfig.expanders[p.expanderIndex].chipType == EXPANDER_TYPE_PCF8575) {
            maxChannel = 15;
        }
        p.i2cChannel = (uint8_t)constrain((int)p.i2cChannel, 0, (int)maxChannel);
    }
    return true;
}

bool ConfigManager::saveHardwareConfig() {
    File f = LittleFS.open("/hardware.json", "w");
    if (!f) {
        Serial.println("[Config] Cannot write /hardware.json");
        return false;
    }
    JsonDocument doc;
    doc["relayCount"]    = _hardwareConfig.relayCount;
    doc["relayInverted"] = _hardwareConfig.relayInverted;
    JsonArray expanders = doc["expanders"].to<JsonArray>();
    for (int i = 0; i < _hardwareConfig.expanderCount; i++) {
        const ExpanderEntry& e = _hardwareConfig.expanders[i];
        JsonObject eo = expanders.add<JsonObject>();
        eo["enabled"]    = e.enabled;
        eo["name"]       = e.name;
        eo["chipType"]   = e.chipType;
        eo["i2cAddress"] = e.i2cAddress;
    }
    JsonArray pumps = doc["pumps"].to<JsonArray>();
    for (int i = 0; i < MAX_RELAY_COUNT; i++) {
        const PumpEntry& p = _hardwareConfig.pumps[i];
        JsonObject po = pumps.add<JsonObject>();
        po["enabled"]       = p.enabled;
        po["name"]          = p.name;
        po["outputType"]    = p.outputType;
        po["pin"]           = p.pin;
        po["expanderIndex"] = p.expanderIndex;
        po["i2cChannel"]    = p.i2cChannel;
        po["invertLogic"]   = p.invertLogic;
        po["maxRuntimeSec"] = p.maxRuntimeSec;
        po["notes"]         = p.notes;
    }
    serializeJson(doc, f);
    f.close();
    Serial.println("[Config] Hardware config saved.");
    return true;
}

// ─── Slot Config (new watering schedule model) ────────────────────────────────

bool ConfigManager::loadSlotConfig() {
    _slotConfig = SlotConfig{};  // reset to defaults

    File f = LittleFS.open("/watering.json", "r");
    if (!f) {
        Serial.println("[Config] /watering.json not found, watering locked.");
        return false;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("[Config] /watering.json parse error: %s\n", err.c_str());
        return false;
    }

    // ── New format: slots + assignments ──────────────────────────────────────
    if (doc["slots"].is<JsonArray>()) {
        JsonArray sArr = doc["slots"].as<JsonArray>();
        int sc = min((int)sArr.size(), MAX_SLOTS);
        _slotConfig.slotCount = sc;
        for (int i = 0; i < sc; i++) {
            WateringSlot& s = _slotConfig.slots[i];
            s = WateringSlot{};
            JsonObject so = sArr[i].as<JsonObject>();
            strlcpy(s.name, so["name"] | "", sizeof(s.name));
            s.enabled         = so["enabled"]       | true;
            s.triggerType     = (uint8_t)constrain((int)(so["triggerType"] | 0), 0, 4);
            s.fixedHour       = (uint8_t)constrain((int)(so["fixedHour"]   | 6), 0, 23);
            s.fixedMinute     = (uint8_t)constrain((int)(so["fixedMinute"] | 0), 0, 59);
            s.offsetMinutes   = (int16_t)constrain((int)(so["offsetMinutes"] | 0), -720, 720);
            s.offsetBase      = (uint8_t)constrain((int)(so["offsetBase"] | 0), 0, 2);
            s.repeatMode      = (uint8_t)constrain((int)(so["repeatMode"] | REPEAT_WEEKDAYS), REPEAT_WEEKDAYS, REPEAT_INTERVAL_DAYS);
            s.days            = (uint8_t)(so["days"] | 0x7F);
            s.intervalDays    = (uint8_t)constrain((int)(so["intervalDays"] | 1), 1, 90);
            s.intervalAnchorDay = (uint16_t)constrain((int)(so["intervalAnchorDay"] | 0), 0, 65535);
            s.skipIfRainMm    = so["skipIfRainMm"]   | 0.0f;
            s.skipIfRainPct   = so["skipIfRainPct"]  | 0.0f;
            s.runOnlyAboveTemp= so["runOnlyAboveTemp"]| -99.0f;
            s.reduceIfRainMm  = so["reduceIfRainMm"] | 0.0f;
            s.reducePct       = (uint8_t)constrain((int)(so["reducePct"] | 50), 1, 99);
        }

        if (doc["weatherTemplates"].is<JsonArray>()) {
            JsonArray wtArr = doc["weatherTemplates"].as<JsonArray>();
            int tc = min((int)wtArr.size(), MAX_WEATHER_TEMPLATES);
            _slotConfig.weatherTemplateCount = tc;
            for (int i = 0; i < tc; i++) {
                WeatherTemplate& wt = _slotConfig.weatherTemplates[i];
                wt = WeatherTemplate{};
                JsonObject wto = wtArr[i].as<JsonObject>();
                strlcpy(wt.name, wto["name"] | "", sizeof(wt.name));
                wt.weather.skipIfRainMm = wto["skipIfRainMm"] | 0.0f;
                wt.weather.skipIfRainPct = wto["skipIfRainPct"] | 0.0f;
                wt.weather.runOnlyAboveTemp = wto["runOnlyAboveTemp"] | -99.0f;
                wt.weather.reduceIfRainMm = wto["reduceIfRainMm"] | 0.0f;
                wt.weather.reducePct = (uint8_t)constrain((int)(wto["reducePct"] | 50), 1, 99);
            }
        }

        if (doc["assignments"].is<JsonArray>()) {
            JsonArray aArr = doc["assignments"].as<JsonArray>();
            int ac = min((int)aArr.size(), MAX_SLOT_ASSIGNMENTS);
            _slotConfig.assignCount = ac;
            for (int j = 0; j < ac; j++) {
                SlotPumpAssignment& a = _slotConfig.assignments[j];
                a = SlotPumpAssignment{};
                JsonObject ao = aArr[j].as<JsonObject>();
                a.weatherTemplateIndex = (int8_t)constrain((int)(ao["weatherTemplateIndex"] | -1), -1, MAX_WEATHER_TEMPLATES - 1);
                a.slotIndex   = (uint8_t)constrain((int)(ao["slotIndex"]   | 0), 0, MAX_SLOTS - 1);
                a.pumpIndex   = (uint8_t)constrain((int)(ao["pumpIndex"]   | 0), 0, MAX_RELAY_COUNT - 1);
                a.durationSec = constrain((int)(ao["durationSec"] | 60), 1, 7200);
                a.useOwnWeatherPolicy = ao["useOwnWeatherPolicy"] | false;
                a.weather.skipIfRainMm = ao["skipIfRainMm"] | 0.0f;
                a.weather.skipIfRainPct = ao["skipIfRainPct"] | 0.0f;
                a.weather.runOnlyAboveTemp = ao["runOnlyAboveTemp"] | -99.0f;
                a.weather.reduceIfRainMm = ao["reduceIfRainMm"] | 0.0f;
                a.weather.reducePct = (uint8_t)constrain((int)(ao["reducePct"] | 50), 1, 99);
            }
        }

        for (int si = 0; si < _slotConfig.slotCount; si++) {
            WateringSlot& slot = _slotConfig.slots[si];
            WeatherPolicy legacyPolicy = makeSlotLegacyPolicy(slot);
            if (!weatherPolicyIsActive(legacyPolicy)) continue;

            int templateIndex = ensureWeatherTemplate(_slotConfig, slot.name, legacyPolicy);
            if (templateIndex >= 0) {
                for (int ai = 0; ai < _slotConfig.assignCount; ai++) {
                    SlotPumpAssignment& a = _slotConfig.assignments[ai];
                    if (a.slotIndex != (uint8_t)si) continue;
                    if (a.weatherTemplateIndex < 0 && !a.useOwnWeatherPolicy) {
                        a.weatherTemplateIndex = (int8_t)templateIndex;
                    }
                }
            }
            clearSlotLegacyWeather(slot);
        }

        for (int ai = 0; ai < _slotConfig.assignCount; ai++) {
            SlotPumpAssignment& a = _slotConfig.assignments[ai];
            if (a.weatherTemplateIndex >= 0) continue;
            if (!a.useOwnWeatherPolicy || !weatherPolicyIsActive(a.weather)) {
                a.useOwnWeatherPolicy = false;
                clearWeatherPolicy(a.weather);
                continue;
            }

            char templateName[32];
            snprintf(templateName, sizeof(templateName), "Wetter %d", _slotConfig.weatherTemplateCount + 1);
            int templateIndex = ensureWeatherTemplate(_slotConfig, templateName, a.weather);
            if (templateIndex >= 0) {
                a.weatherTemplateIndex = (int8_t)templateIndex;
                a.useOwnWeatherPolicy = false;
                clearWeatherPolicy(a.weather);
            }
        }

        Serial.printf("[Config] Slot config loaded: %d slots, %d assignments.\n",
                      _slotConfig.slotCount, _slotConfig.assignCount);
        return true;
    }

    // ── Old format (entries[]): migrate to slot model ─────────────────────────
    if (doc["entries"].is<JsonArray>()) {
        JsonArray entries = doc["entries"].as<JsonArray>();
        for (JsonObject e : entries) {
            if (_slotConfig.slotCount >= MAX_SLOTS) break;
            if (_slotConfig.assignCount >= MAX_SLOT_ASSIGNMENTS) break;

            int si = _slotConfig.slotCount++;
            WateringSlot& s = _slotConfig.slots[si];
            s = WateringSlot{};
            char nameBuf[32];
            snprintf(nameBuf, sizeof(nameBuf), "Slot %d", si + 1);
            strlcpy(s.name, nameBuf, sizeof(s.name));
            s.enabled     = e["active"] | true;
            s.triggerType = TRIGGER_FIXED_TIME;
            s.fixedHour   = (uint8_t)constrain((int)(e["hour"]   | 6), 0, 23);
            s.fixedMinute = (uint8_t)constrain((int)(e["minute"] | 0), 0, 59);
            s.days        = (uint8_t)(e["days"] | 0x7F);
            s.repeatMode  = REPEAT_WEEKDAYS;
            s.intervalDays = 1;
            s.intervalAnchorDay = 0;

            int aj = _slotConfig.assignCount++;
            SlotPumpAssignment& a = _slotConfig.assignments[aj];
            a.slotIndex   = (uint8_t)si;
            a.pumpIndex   = (uint8_t)constrain((int)(e["relay"]       | 0), 0, MAX_RELAY_COUNT - 1);
            a.durationSec = constrain((int)(e["durationSec"] | 60), 1, 7200);
        }
        Serial.printf("[Config] Old watering format migrated: %d slot(s).\n", _slotConfig.slotCount);
        // Save in new format immediately so migration runs only once
        saveSlotConfig();
        return true;
    }

    Serial.println("[Config] /watering.json: no recognisable data.");
    return false;
}

bool ConfigManager::saveSlotConfig() {
    File f = LittleFS.open("/watering.json", "w");
    if (!f) {
        Serial.println("[Config] Cannot write /watering.json");
        return false;
    }
    JsonDocument doc;
    JsonArray sArr = doc["slots"].to<JsonArray>();
    for (int i = 0; i < _slotConfig.slotCount; i++) {
        const WateringSlot& s = _slotConfig.slots[i];
        JsonObject so = sArr.add<JsonObject>();
        so["name"]           = s.name;
        so["enabled"]        = s.enabled;
        so["triggerType"]    = s.triggerType;
        so["fixedHour"]      = s.fixedHour;
        so["fixedMinute"]    = s.fixedMinute;
        so["offsetMinutes"]  = s.offsetMinutes;
        so["offsetBase"]     = s.offsetBase;
        so["repeatMode"]     = s.repeatMode;
        so["days"]           = s.days;
        so["intervalDays"]   = s.intervalDays;
        so["intervalAnchorDay"] = s.intervalAnchorDay;
        so["skipIfRainMm"]   = s.skipIfRainMm;
        so["skipIfRainPct"]  = s.skipIfRainPct;
        so["runOnlyAboveTemp"] = s.runOnlyAboveTemp;
        so["reduceIfRainMm"] = s.reduceIfRainMm;
        so["reducePct"]      = s.reducePct;
    }
    JsonArray wtArr = doc["weatherTemplates"].to<JsonArray>();
    for (int i = 0; i < _slotConfig.weatherTemplateCount; i++) {
        const WeatherTemplate& wt = _slotConfig.weatherTemplates[i];
        JsonObject wto = wtArr.add<JsonObject>();
        wto["name"] = wt.name;
        wto["skipIfRainMm"] = wt.weather.skipIfRainMm;
        wto["skipIfRainPct"] = wt.weather.skipIfRainPct;
        wto["runOnlyAboveTemp"] = wt.weather.runOnlyAboveTemp;
        wto["reduceIfRainMm"] = wt.weather.reduceIfRainMm;
        wto["reducePct"] = wt.weather.reducePct;
    }
    JsonArray aArr = doc["assignments"].to<JsonArray>();
    for (int j = 0; j < _slotConfig.assignCount; j++) {
        const SlotPumpAssignment& a = _slotConfig.assignments[j];
        JsonObject ao = aArr.add<JsonObject>();
        ao["weatherTemplateIndex"] = a.weatherTemplateIndex;
        ao["slotIndex"]   = a.slotIndex;
        ao["pumpIndex"]   = a.pumpIndex;
        ao["durationSec"] = a.durationSec;
        ao["useOwnWeatherPolicy"] = a.useOwnWeatherPolicy;
        ao["skipIfRainMm"] = a.weather.skipIfRainMm;
        ao["skipIfRainPct"] = a.weather.skipIfRainPct;
        ao["runOnlyAboveTemp"] = a.weather.runOnlyAboveTemp;
        ao["reduceIfRainMm"] = a.weather.reduceIfRainMm;
        ao["reducePct"] = a.weather.reducePct;
    }
    serializeJson(doc, f);
    f.close();
    Serial.printf("[Config] Slot config saved: %d slot(s), %d assignment(s).\n",
                  _slotConfig.slotCount, _slotConfig.assignCount);
    return true;
}

bool ConfigManager::isWateringConfigValid() const {
    return _slotConfig.slotCount > 0 &&
           _slotConfig.assignCount > 0 &&
           _hardwareConfig.relayCount > 0;
}

bool ConfigManager::resetAll() {
    bool ok = true;
    if (LittleFS.exists("/config.json"))   ok &= LittleFS.remove("/config.json");
    if (LittleFS.exists("/hardware.json")) ok &= LittleFS.remove("/hardware.json");
    if (LittleFS.exists("/watering.json")) ok &= LittleFS.remove("/watering.json");
    // Reinitialise to defaults
    _deviceConfig   = DeviceConfig{};
    _hardwareConfig = HardwareConfig{};
    _slotConfig     = SlotConfig{};
    Serial.println("[Config] All configs reset to defaults.");
    return ok;
}

// (end of ConfigManager.cpp)
