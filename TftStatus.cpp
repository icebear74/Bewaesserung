#include "TftStatus.h"

TftStatus::TftStatus(int csPin, int dcPin, int rstPin)
    : _tft(csPin, dcPin, rstPin) {}

bool TftStatus::begin() {
    _tft.initR(INITR_BLACKTAB);   // standard 1.8" ST7735R (black-tab module)
    _tft.setRotation(1);          // landscape: 160 × 128
    _tft.setTextWrap(false);
    _tft.fillScreen(C_BLACK);
    _initialized = true;
    Serial.println("[TFT] ST7735 initialized (landscape 160x128).");
    return true;
}

// ─── Internal draw helpers ─────────────────────────────────────────────────────

void TftStatus::drawSeparator(int y) {
    _tft.drawFastHLine(0, y, TFT_W, C_DGRAY);
}

void TftStatus::drawHeader(const char* timeStr) {
    _tft.fillRect(0, HDR_Y, TFT_W, HDR_H, C_NAVY);
    _tft.setTextColor(C_WHITE);
    _tft.setTextSize(2);
    // "HH:MM:SS" = 8 chars × 12 px = 96 px → centre: (160 - 96) / 2 = 32
    _tft.setCursor(32, HDR_Y + 5);
    _tft.print(timeStr ? timeStr : "--:--:--");
}

void TftStatus::drawInfoRow(const char* ip, const char* statusText,
                             uint16_t statusColor) {
    _tft.fillRect(0, INFO_Y, TFT_W, INFO_H, C_BLACK);
    _tft.setTextSize(1);

    _tft.setTextColor(C_LGRAY);
    _tft.setCursor(2, INFO_Y + 2);
    _tft.print(ip ? ip : "---");

    _tft.setTextColor(statusColor);
    _tft.setCursor(2, INFO_Y + 11);
    _tft.print(statusText ? statusText : "");
}

void TftStatus::drawPumpBar(const bool* active, const bool* enabled, int count) {
    _tft.fillRect(0, PUMP_Y, TFT_W, PUMP_H, C_BLACK);

    for (int i = 0; i < 8; i++) {
        int bx = i * BADGE_W;

        bool isActive  = (i < count) && active  && active[i];
        bool isEnabled = (i < count) && enabled && enabled[i];

        uint16_t bgColor, fgColor;
        const char* label;

        if (isActive) {
            bgColor = C_GREEN;
            fgColor = C_BLACK;
            label   = "EIN";
        } else if (isEnabled) {
            bgColor = C_RED;
            fgColor = C_WHITE;
            label   = "AUS";
        } else {
            bgColor = C_DGRAY;
            fgColor = C_LGRAY;
            label   = "---";
        }

        // Badge background (1-px gap between badges)
        _tft.fillRect(bx + 1, PUMP_Y + 1, BADGE_W - 2, PUMP_H - 2, bgColor);

        // Pump number (scale=2, 12×16 px) – centred horizontally in badge.
        // For digits 1-8 that is a single character.
        // Total content height: number(16) + gap(3) + label(8) = 27 px
        // Vertical centre offset: (54 - 27) / 2 = 13 px from PUMP_Y
        char numBuf[3];
        snprintf(numBuf, sizeof(numBuf), "%d", i + 1);
        _tft.setTextColor(fgColor);
        _tft.setTextSize(2);
        int nx = bx + (BADGE_W - 12) / 2;   // (20 - 12) / 2 = 4
        _tft.setCursor(nx, PUMP_Y + 13);
        _tft.print(numBuf);

        // Status label (scale=1, 6 px per char): "EIN" = 18 px, "AUS"/"---" = 18 px
        _tft.setTextSize(1);
        int lx = bx + (BADGE_W - (int)strlen(label) * 6) / 2;
        if (lx < bx) lx = bx;
        _tft.setCursor(lx, PUMP_Y + 32);
        _tft.print(label);
    }
}

void TftStatus::drawFooter(const char* text) {
    _tft.fillRect(0, FOOT_Y, TFT_W, FOOT_H, C_BLACK);
    _tft.setTextColor(C_DGRAY);
    _tft.setTextSize(1);
    _tft.setCursor(2, FOOT_Y + 2);
    _tft.print(text ? text : "");
}

void TftStatus::drawDashboard(const char* timeStr, const char* ip,
                               const char* statusText, uint16_t statusColor,
                               const char* footerText,
                               const bool* pumpActive, const bool* pumpEnabled,
                               int pumpCount) {
    drawHeader(timeStr);
    drawSeparator(HDR_Y + HDR_H);      // y = 26
    drawInfoRow(ip, statusText, statusColor);
    drawSeparator(PUMP_Y - 1);         // y = 45
    drawPumpBar(pumpActive, pumpEnabled, pumpCount);
    drawSeparator(PUMP_Y + PUMP_H);    // y = 100
    drawFooter(footerText);
}

// ─── Simple message screen (boot / transition states) ─────────────────────────

void TftStatus::showMessage(const char* line1, const char* line2,
                             const char* line3) {
    if (!_initialized) return;
    _tft.fillScreen(C_BLACK);

    // Small title bar
    _tft.fillRect(0, 0, TFT_W, 18, C_NAVY);
    _tft.setTextColor(C_LGRAY);
    _tft.setTextSize(1);
    _tft.setCursor(2, 5);
    _tft.print("Bewaesserung");

    _tft.setTextSize(1);
    if (line1) {
        _tft.setTextColor(C_WHITE);
        _tft.setCursor(4, 32);
        _tft.print(line1);
    }
    if (line2) {
        _tft.setTextColor(C_LGRAY);
        _tft.setCursor(4, 50);
        _tft.print(line2);
    }
    if (line3) {
        _tft.setTextColor(C_LGRAY);
        _tft.setCursor(4, 68);
        _tft.print(line3);
    }
}

// ─── Specific transitional screens ────────────────────────────────────────────

void TftStatus::showBoot() {
    if (!_initialized) return;
    _tft.fillScreen(C_NAVY);
    _tft.setTextColor(C_WHITE);
    _tft.setTextSize(2);
    // "Bewaesserung" = 13 chars × 12 = 156 px, fits in 160 px with 2 px margin
    _tft.setCursor(2, 28);
    _tft.print("Bewaesserung");
    _tft.setTextSize(1);
    _tft.setTextColor(C_LGRAY);
    _tft.setCursor(4, 60);
    _tft.print("Booting...");
}

void TftStatus::showDs3231Status(bool present) {
    if (!_initialized) return;
    if (present) {
        showMessage("RTC DS3231", "Erkannt OK");
    } else {
        showMessage("RTC DS3231", "NICHT erkannt!", "Notbetrieb!");
    }
}

void TftStatus::showWifiAttempt(int apIndex, int apCount, const char* ssid) {
    if (!_initialized) return;
    char line2[32];
    snprintf(line2, sizeof(line2), "AP %d/%d", apIndex, apCount);
    char line3[32];
    snprintf(line3, sizeof(line3), "%.26s", ssid ? ssid : "");
    showMessage("Verbinde WLAN...", line2, line3);
}

void TftStatus::showWPS() {
    if (!_initialized) return;
    showMessage("WPS Modus...", "Router-Taste", "druecken!");
}

void TftStatus::showApMode(const char* apSSID, const char* ip) {
    if (!_initialized) return;
    _tft.fillScreen(C_BLACK);
    _tft.fillRect(0, 0, TFT_W, 18, C_NAVY);
    _tft.setTextColor(C_LGRAY);
    _tft.setTextSize(1);
    _tft.setCursor(2, 5);
    _tft.print("Setup-Modus (AP)");

    _tft.setTextColor(C_WHITE);
    _tft.setCursor(4, 28);
    _tft.print("SSID:");
    _tft.setTextColor(C_YELLOW);
    _tft.setCursor(4, 40);
    char ssidBuf[27];
    snprintf(ssidBuf, sizeof(ssidBuf), "%.26s", apSSID ? apSSID : "");
    _tft.print(ssidBuf);

    _tft.setTextColor(C_WHITE);
    _tft.setCursor(4, 60);
    _tft.print("IP:");
    _tft.setTextColor(C_YELLOW);
    _tft.setCursor(4, 72);
    _tft.print(ip ? ip : "192.168.4.1");

    _tft.setTextColor(C_LGRAY);
    _tft.setCursor(4, 92);
    _tft.print("Kein Passwort");
}

void TftStatus::showState(SystemState state) {
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
        case SystemState::RESTARTING:
            showMessage("Neustart...");
            break;
        default:
            break;
    }
}

// ─── Periodic dashboard update ─────────────────────────────────────────────────

void TftStatus::update(SystemState state, const String& ip, const String& apSSID,
                        const bool* pumpActive, const bool* pumpEnabled,
                        int pumpCount) {
    if (!_initialized) return;

    unsigned long now = millis();
    if (now - _lastUpdate < UPDATE_INTERVAL_MS) return;
    _lastUpdate = now;

    switch (state) {
        case SystemState::WIFI_AP_MODE:
            showApMode(apSSID.c_str(), "192.168.4.1");
            return;

        case SystemState::SETUP_REQUIRED:
            showMessage("Ersteinrichtung", "Bitte Browser",
                        ip.isEmpty() ? "192.168.4.1" : ip.c_str());
            return;

        case SystemState::RUNNING:
        case SystemState::RUNNING_OFFLINE: {
            // ── Build time string ──────────────────────────────────────────
            time_t now_t;
            time(&now_t);
            struct tm tm_buf;
            struct tm* tmi = localtime_r(&now_t, &tm_buf);
            char tbuf[12];
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d",
                     tmi->tm_hour, tmi->tm_min, tmi->tm_sec);

            // ── Build footer: date ─────────────────────────────────────────
            char fbuf[32];
            snprintf(fbuf, sizeof(fbuf), "%02d.%02d.%04d",
                     tmi->tm_mday, tmi->tm_mon + 1, tmi->tm_year + 1900);

            // ── Status text & colour ───────────────────────────────────────
            const char* statusText;
            uint16_t    statusColor;
            if (state == SystemState::RUNNING_OFFLINE) {
                statusText  = "Offline-Betrieb";
                statusColor = C_ORANGE;
            } else {
                statusText  = "Betrieb OK";
                statusColor = C_GREEN;
            }

            drawDashboard(tbuf, ip.c_str(), statusText, statusColor,
                          fbuf, pumpActive, pumpEnabled, pumpCount);
            return;
        }

        default:
            return;
    }
}
