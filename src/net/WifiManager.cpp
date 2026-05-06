#include "WifiManager.h"
#include <esp_wps.h>
#include <vector>
#include <algorithm>

// WPS config for Push-Button mode
static esp_wps_config_t wps_config = WPS_CONFIG_INIT_DEFAULT(WPS_TYPE_PBC);

WifiManager::WifiManager() {}

bool WifiManager::begin(DeviceConfig& config) {
    _config = &config;

    if (strlen(config.ssid) == 0) {
        Serial.println("[WiFi] No SSID configured, starting AP mode.");
        startApMode();
        return false;
    }

    Serial.printf("[WiFi] Attempting connection to SSID: %s\n", config.ssid);
    if (connectMultiAP(config)) {
        return true;
    }

    Serial.println("[WiFi] Multi-AP connect failed. Trying WPS...");
    if (tryWPS()) {
        return true;
    }

    Serial.println("[WiFi] WPS failed. Starting AP mode.");
    startApMode();
    return false;
}

void WifiManager::update() {
    if (_apModeActive || _connected) return;

    unsigned long now = millis();
    if (now - _lastReconnectAttempt < RECONNECT_INTERVAL_MS) return;
    _lastReconnectAttempt = now;

    if (_config) {
        Serial.println("[WiFi] Attempting reconnection...");
        if (connectMultiAP(*_config)) {
            Serial.printf("[WiFi] Reconnected. IP: %s\n", _localIP.c_str());
        }
    }
}

void WifiManager::reconnect(DeviceConfig& config) {
    _config    = &config;
    _connected = false;
    WiFi.disconnect(true);
    delay(200);
    connectMultiAP(config);
}

// ─── Private ──────────────────────────────────────────────────────────────────

bool WifiManager::connectMultiAP(DeviceConfig& config) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);

    Serial.println("[WiFi] Scanning networks...");
    int found = WiFi.scanNetworks();
    if (found <= 0) {
        Serial.println("[WiFi] No networks found.");
        return false;
    }

    // Collect APs matching our SSID, sort by RSSI descending
    struct APCandidate {
        int    rssi;
        int    channel;
        uint8_t bssid[6];
    };
    std::vector<APCandidate> candidates;

    for (int i = 0; i < found; i++) {
        if (WiFi.SSID(i) == String(config.ssid)) {
            APCandidate c;
            c.rssi    = WiFi.RSSI(i);
            c.channel = WiFi.channel(i);
            uint8_t* b = WiFi.BSSID(i);
            memcpy(c.bssid, b, 6);
            candidates.push_back(c);
        }
    }

    if (candidates.empty()) {
        Serial.printf("[WiFi] SSID '%s' not found in scan.\n", config.ssid);
        WiFi.scanDelete();
        return false;
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const APCandidate& a, const APCandidate& b){ return a.rssi > b.rssi; });

    Serial.printf("[WiFi] Found %d AP(s) with SSID '%s'.\n",
                  (int)candidates.size(), config.ssid);
    WiFi.scanDelete();

    for (int idx = 0; idx < (int)candidates.size(); idx++) {
        APCandidate& c = candidates[idx];
        Serial.printf("[WiFi] Trying AP %d/%d (RSSI %d, ch %d)...\n",
                      idx + 1, (int)candidates.size(), c.rssi, c.channel);

        WiFi.begin(config.ssid, config.password, c.channel, c.bssid);

        unsigned long t = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - t) < CONNECT_TIMEOUT_MS) {
            delay(200);
            yield();
        }

        if (WiFi.status() == WL_CONNECTED) {
            _connected = true;
            _localIP   = WiFi.localIP().toString();
            WiFi.setHostname(config.hostname);
            Serial.printf("[WiFi] Connected! IP: %s\n", _localIP.c_str());
            return true;
        }
        WiFi.disconnect(true);
        delay(100);
    }

    Serial.println("[WiFi] All AP candidates failed.");
    return false;
}

bool WifiManager::tryWPS() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);

    Serial.println("[WiFi] Starting WPS (PBC mode). Press the WPS button on your router...");

    volatile bool wpsSuccess = false;
    volatile bool wpsDone    = false;

    WiFi.onEvent([&](arduino_event_id_t event, arduino_event_info_t /*info*/) {
        switch (event) {
            case ARDUINO_EVENT_WPS_ER_SUCCESS:
                Serial.println("[WiFi] WPS success!");
                wpsSuccess = true;
                wpsDone    = true;
                break;
            case ARDUINO_EVENT_WPS_ER_FAILED:
                Serial.println("[WiFi] WPS failed.");
                wpsDone = true;
                break;
            case ARDUINO_EVENT_WPS_ER_TIMEOUT:
                Serial.println("[WiFi] WPS timeout.");
                wpsDone = true;
                break;
            default:
                break;
        }
    });

    esp_wifi_wps_enable(&wps_config);
    esp_wifi_wps_start(0);

    unsigned long start = millis();
    while (!wpsDone && (millis() - start) < WPS_TIMEOUT_MS) {
        delay(100);
        yield();
    }

    esp_wifi_wps_disable();

    if (wpsSuccess) {
        unsigned long t2 = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - t2) < 10000UL) {
            delay(200);
        }
        if (WiFi.status() == WL_CONNECTED) {
            _connected = true;
            _localIP   = WiFi.localIP().toString();
            Serial.printf("[WiFi] WPS connected. IP: %s\n", _localIP.c_str());
            return true;
        }
    }
    return false;
}

void WifiManager::startApMode() {
    _apModeActive = true;
    _apSSID       = "Bewaesserung-Setup";
    WiFi.mode(WIFI_AP);
    WiFi.softAP(_apSSID.c_str());
    Serial.printf("[WiFi] AP started: %s, IP: %s\n",
        _apSSID.c_str(), WiFi.softAPIP().toString().c_str());
}
