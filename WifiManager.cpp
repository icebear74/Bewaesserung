#include "WifiManager.h"
#include <esp_wps.h>
#include <vector>
#include <algorithm>

// WPS config for Push-Button mode
static esp_wps_config_t wps_config = WPS_CONFIG_INIT_DEFAULT(WPS_TYPE_PBC);
// File-scope WPS state flags (safe for use inside WiFi event callbacks)
static volatile bool s_wpsSuccess = false;
static volatile bool s_wpsDone    = false;

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
    if (_apModeActive) return;

    unsigned long now = millis();

    // ── Health-check for seemingly connected but dead links ───────────────────
    if (_connected) {
        if (now - _lastHealthCheckMs >= HEALTH_CHECK_INTERVAL_MS) {
            _lastHealthCheckMs = now;
            if (!healthCheck()) {
                Serial.println("[WiFi] Health check failed – forcing reconnect.");
                _connected = false;
                _localIP   = "";
                _ssid      = "";
                _connectedSinceEpoch = 0;
                // Trigger reconnect immediately on next update() call
                _lastReconnectAttempt = now - RECONNECT_INTERVAL_MS;
            }
        }
        return;
    }

    // ── Reconnect when disconnected ───────────────────────────────────────────
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
    _localIP   = "";
    _ssid      = "";
    _connectedSinceEpoch = 0;
    WiFi.disconnect(true);
    delay(200);
    connectMultiAP(config);
}

// ─── Private ──────────────────────────────────────────────────────────────────

bool WifiManager::healthCheck() {
    wl_status_t status = WiFi.status();
    if (status != WL_CONNECTED) {
        Serial.printf("[WiFi] Health check: status=%d (not connected).\n", (int)status);
        return false;
    }
    IPAddress localIp = WiFi.localIP();
    if (localIp == IPAddress(0, 0, 0, 0)) {
        Serial.println("[WiFi] Health check: local IP is 0.0.0.0.");
        return false;
    }
    IPAddress gateway = WiFi.gatewayIP();
    if (gateway == IPAddress(0, 0, 0, 0)) {
        Serial.println("[WiFi] Health check: gateway IP is 0.0.0.0.");
        return false;
    }
    int rssi = WiFi.RSSI();
    Serial.printf("[WiFi] Health check OK (IP=%s, GW=%s, RSSI=%d dBm).\n",
                  localIp.toString().c_str(), gateway.toString().c_str(), rssi);
    return true;
}

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
            _ssid      = String(config.ssid);
            time_t t = time(nullptr);
            _connectedSinceEpoch = (t > 1000000L) ? t : 0;
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

    // Reset file-scope flags (static storage – safe to reference from event callbacks)
    s_wpsSuccess = false;
    s_wpsDone    = false;

    WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t /*info*/) {
        switch (event) {
            case ARDUINO_EVENT_WPS_ER_SUCCESS:
                Serial.println("[WiFi] WPS success!");
                s_wpsSuccess = true;
                s_wpsDone    = true;
                break;
            case ARDUINO_EVENT_WPS_ER_FAILED:
                Serial.println("[WiFi] WPS failed.");
                s_wpsDone = true;
                break;
            case ARDUINO_EVENT_WPS_ER_TIMEOUT:
                Serial.println("[WiFi] WPS timeout.");
                s_wpsDone = true;
                break;
            default:
                break;
        }
    });

    esp_wifi_wps_enable(&wps_config);
    esp_wifi_wps_start(0);

    unsigned long start = millis();
    while (!s_wpsDone && (millis() - start) < WPS_TIMEOUT_MS) {
        delay(100);
        yield();
    }

    esp_wifi_wps_disable();

    if (s_wpsSuccess) {
        unsigned long t2 = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - t2) < 10000UL) {
            delay(200);
        }
        if (WiFi.status() == WL_CONNECTED) {
            _connected = true;
            _localIP   = WiFi.localIP().toString();
            _ssid      = WiFi.SSID();
            time_t t = time(nullptr);
            _connectedSinceEpoch = (t > 1000000L) ? t : 0;
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
