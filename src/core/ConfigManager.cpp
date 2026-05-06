#include "ConfigManager.h"
#include <LittleFS.h>

ConfigManager::ConfigManager() {}

void ConfigManager::begin() {
    if (!LittleFS.begin(true)) {
        Serial.println("[Config] LittleFS mount failed, formatted.");
    } else {
        Serial.println("[Config] LittleFS mounted.");
    }
    loadDeviceConfig();
    loadHardwareConfig();
    loadWateringConfig();
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
    _hardwareConfig.relayCount   = doc["relayCount"]   | 0;
    _hardwareConfig.relayInverted = doc["relayInverted"] | false;
    JsonArray pins = doc["relayPins"].as<JsonArray>();
    for (int i = 0; i < MAX_RELAY_COUNT; i++) {
        _hardwareConfig.relayPins[i] = (i < (int)pins.size()) ? (int)pins[i] : -1;
    }
    Serial.printf("[Config] Hardware config loaded: %d relays.\n", _hardwareConfig.relayCount);
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
    JsonArray pins = doc["relayPins"].to<JsonArray>();
    for (int i = 0; i < MAX_RELAY_COUNT; i++) {
        pins.add(_hardwareConfig.relayPins[i]);
    }
    serializeJson(doc, f);
    f.close();
    Serial.println("[Config] Hardware config saved.");
    return true;
}

// ─── Watering Config ──────────────────────────────────────────────────────────

bool ConfigManager::loadWateringConfig() {
    File f = LittleFS.open("/watering.json", "r");
    if (!f) {
        Serial.println("[Config] /watering.json not found, watering locked.");
        _wateringConfig.count = 0;
        return false;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("[Config] /watering.json parse error: %s\n", err.c_str());
        _wateringConfig.count = 0;
        return false;
    }
    _wateringConfig.count = 0;
    JsonArray entries = doc["entries"].as<JsonArray>();
    for (JsonObject e : entries) {
        if (_wateringConfig.count >= MAX_WATERING_ENTRIES) break;
        WateringEntry& we = _wateringConfig.entries[_wateringConfig.count++];
        we.relay       = e["relay"]       | 0;
        we.hour        = e["hour"]        | 0;
        we.minute      = e["minute"]      | 0;
        we.durationSec = e["durationSec"] | 0;
        we.active      = e["active"]      | true;
        we.days        = e["days"]        | 0x7F;
    }
    Serial.printf("[Config] Watering config loaded: %d entries.\n", _wateringConfig.count);
    return true;
}

bool ConfigManager::saveWateringConfig() {
    File f = LittleFS.open("/watering.json", "w");
    if (!f) {
        Serial.println("[Config] Cannot write /watering.json");
        return false;
    }
    JsonDocument doc;
    JsonArray entries = doc["entries"].to<JsonArray>();
    for (int i = 0; i < _wateringConfig.count; i++) {
        WateringEntry& we = _wateringConfig.entries[i];
        JsonObject e = entries.add<JsonObject>();
        e["relay"]       = we.relay;
        e["hour"]        = we.hour;
        e["minute"]      = we.minute;
        e["durationSec"] = we.durationSec;
        e["active"]      = we.active;
        e["days"]        = we.days;
    }
    serializeJson(doc, f);
    f.close();
    Serial.println("[Config] Watering config saved.");
    return true;
}

bool ConfigManager::isWateringConfigValid() const {
    return _wateringConfig.count > 0 && _hardwareConfig.relayCount > 0;
}

bool ConfigManager::resetAll() {
    bool ok = true;
    if (LittleFS.exists("/config.json"))   ok &= LittleFS.remove("/config.json");
    if (LittleFS.exists("/hardware.json")) ok &= LittleFS.remove("/hardware.json");
    if (LittleFS.exists("/watering.json")) ok &= LittleFS.remove("/watering.json");
    // Reinitialise to defaults
    _deviceConfig   = DeviceConfig{};
    _hardwareConfig = HardwareConfig{};
    _wateringConfig = WateringConfig{};
    Serial.println("[Config] All configs reset to defaults.");
    return ok;
}
