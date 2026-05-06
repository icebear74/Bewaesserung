#include "OledStatus.h"

OledStatus::OledStatus()
    : _u8g2(U8G2_R0, U8X8_PIN_NONE) {}

bool OledStatus::begin() {
    if (!_u8g2.begin()) {
        Serial.println("[OLED] Init failed.");
        return false;
    }
    _initialized = true;
    _u8g2.setFont(u8g2_font_ncenB08_tr);
    Serial.println("[OLED] Initialized.");
    return true;
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

void OledStatus::showMessage(const char* line1, const char* line2, const char* line3) {
    if (!_initialized) return;
    _u8g2.clearBuffer();
    _u8g2.setFont(u8g2_font_ncenB08_tr);
    if (line1) _u8g2.drawStr(0, 14, line1);
    if (line2) _u8g2.drawStr(0, 30, line2);
    if (line3) _u8g2.drawStr(0, 46, line3);
    _u8g2.sendBuffer();
}

// ─── Specific screens ─────────────────────────────────────────────────────────

void OledStatus::showBoot() {
    if (!_initialized) return;
    _u8g2.clearBuffer();
    _u8g2.setFont(u8g2_font_ncenB14_tr);
    _u8g2.drawStr(10, 24, "Bew\xE4sserung");
    _u8g2.setFont(u8g2_font_ncenB08_tr);
    _u8g2.drawStr(28, 44, "Phase 1 v1.0");
    _u8g2.drawStr(36, 58, "Booting...");
    _u8g2.sendBuffer();
}

void OledStatus::showState(SystemState state) {
    if (!_initialized) return;
    switch (state) {
        case SystemState::LOADING_CONFIG:
            showMessage("Lade Konfiguration", "LittleFS...");
            break;
        case SystemState::WIFI_SCANNING:
            showMessage("WLAN suchen...");
            break;
        case SystemState::WIFI_CONNECTING:
            showMessage("Verbinde mit WLAN...");
            break;
        case SystemState::WIFI_WPS:
            showWPS();
            break;
        case SystemState::TIME_SYNC:
            showMessage("Zeitsynchronisation", "NTP...");
            break;
        case SystemState::WIFI_CONNECTED:
            showMessage("WLAN verbunden!");
            break;
        case SystemState::RUNNING:
        case SystemState::RUNNING_OFFLINE:
        case SystemState::SETUP_REQUIRED:
            // These are handled by update()
            break;
        case SystemState::RESTARTING:
            showMessage("Neustart...");
            break;
        default:
            showMessage("Unbekannt");
            break;
    }
}

void OledStatus::showDs3231Status(bool present) {
    if (!_initialized) return;
    if (present) {
        showMessage("RTC DS3231", "Erkannt OK");
    } else {
        showMessage("RTC DS3231", "NICHT erkannt!", "Notbetrieb!");
    }
}

void OledStatus::showWifiAttempt(int apIndex, int apCount, const char* ssid) {
    if (!_initialized) return;
    char line2[32];
    snprintf(line2, sizeof(line2), "AP %d/%d", apIndex, apCount);
    char line3[32];
    snprintf(line3, sizeof(line3), "%.20s", ssid);
    showMessage("Verbinde WLAN...", line2, line3);
}

void OledStatus::showWPS() {
    if (!_initialized) return;
    showMessage("WPS Modus...", "Router-Taste", "druecken!");
}

void OledStatus::showApMode(const char* apSSID, const char* ip) {
    if (!_initialized) return;
    _u8g2.clearBuffer();
    _u8g2.setFont(u8g2_font_ncenB08_tr);
    _u8g2.drawStr(0, 12, "Setup-AP:");
    // Truncate long SSID to fit display width
    char ssidBuf[22];
    snprintf(ssidBuf, sizeof(ssidBuf), "%.21s", apSSID);
    _u8g2.drawStr(0, 28, ssidBuf);
    char ipBuf[22];
    snprintf(ipBuf, sizeof(ipBuf), "IP: %.16s", ip);
    _u8g2.drawStr(0, 44, ipBuf);
    _u8g2.drawStr(0, 58, "Kein Passwort");
    _u8g2.sendBuffer();
}

void OledStatus::showNormal(const char* ip, const char* timeStr,
                             bool ds3231Missing, bool wateringLocked) {
    if (!_initialized) return;
    _u8g2.clearBuffer();
    _u8g2.setFont(u8g2_font_ncenB08_tr);

    // Time (large-ish)
    _u8g2.setFont(u8g2_font_ncenB14_tr);
    _u8g2.drawStr(18, 20, timeStr ? timeStr : "--:--:--");
    _u8g2.setFont(u8g2_font_ncenB08_tr);

    // IP
    char ipLine[22];
    snprintf(ipLine, sizeof(ipLine), "IP: %.16s", ip ? ip : "---");
    _u8g2.drawStr(0, 36, ipLine);

    // Warnings
    if (ds3231Missing && wateringLocked) {
        _u8g2.drawStr(0, 50, "! Kein RTC | Kein Plan");
    } else if (ds3231Missing) {
        _u8g2.drawStr(0, 50, "! RTC fehlt");
    } else if (wateringLocked) {
        _u8g2.drawStr(0, 50, "! Kein Bew\xE4ss.-Plan");
    } else {
        _u8g2.drawStr(0, 50, "Betrieb OK");
    }

    _u8g2.sendBuffer();
}

// ─── Periodic update ──────────────────────────────────────────────────────────

void OledStatus::update(SystemState state, const String& ip, const String& apSSID) {
    if (!_initialized) return;

    unsigned long now = millis();
    if (now - _lastUpdate < UPDATE_INTERVAL_MS) return;
    _lastUpdate = now;

    switch (state) {
        case SystemState::WIFI_AP_MODE:
            showApMode(apSSID.c_str(), "192.168.4.1");
            break;
        case SystemState::SETUP_REQUIRED:
            showMessage("Ersteinrichtung", "Bitte Browser", ip.isEmpty() ? "192.168.4.1" : ip.c_str());
            break;
        case SystemState::RUNNING:
        case SystemState::RUNNING_OFFLINE: {
            // Time from system clock – use localtime_r() which respects the TZ set by TimeSync
            time_t now_t;
            time(&now_t);
            struct tm tm_buf;
            struct tm* tm_info = localtime_r(&now_t, &tm_buf);
            char tbuf[12];
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d",
                     tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
            bool offlineMode = (state == SystemState::RUNNING_OFFLINE);
            showNormal(ip.c_str(), tbuf, false, offlineMode);
            break;
        }
        default:
            break;
    }
}
