#include "WebHandlers.h"
#include "WebServer.h"
#include "WebPages.h"
#include "core/Application.h"
#include "core/ConfigManager.h"
#include "core/StateManager.h"
#include "net/WifiManager.h"
#include "net/TimeSync.h"
#include "hw/Ds3231Manager.h"
#include "hw/RelayManager.h"
#include <WebServer.h>
#include <WiFi.h>

// ─── Module-level state ───────────────────────────────────────────────────────

static WebServerManager* g_wsm    = nullptr;
static Application*      g_app    = nullptr;
static WebServer*         g_server = nullptr;

// ─── Utility: string replace ──────────────────────────────────────────────────

static String replaceToken(String html, const char* token, const String& value) {
    html.replace(token, value);
    return html;
}

// Build a full page by concatenating header + body + footer
static String buildPage(const char* bodyPgm) {
    String page;
    page.reserve(4096);
    page += FPSTR(HTML_HEADER);
    page += FPSTR(bodyPgm);
    page += FPSTR(HTML_FOOTER);
    return page;
}

// Format uptime as Xd Xh Xm Xs
static String formatUptime() {
    unsigned long ms = millis();
    unsigned long s  = ms / 1000;
    unsigned long m  = s / 60;
    unsigned long h  = m / 60;
    unsigned long d  = h / 24;
    char buf[48];
    snprintf(buf, sizeof(buf), "%lud %02luh %02lum %02lus", d, h % 24, m % 60, s % 60);
    return String(buf);
}

// ─── Common warning fragments ─────────────────────────────────────────────────

static String ds3231WarningHtml() {
    if (!g_app->isDs3231Present()) return FPSTR(HTML_DS3231_WARNING);
    return "";
}

static String wateringLockedWarningHtml() {
    if (g_app->isWateringLocked()) return FPSTR(HTML_WATERING_LOCKED_WARNING);
    return "";
}

// ─── Handler forward declarations ────────────────────────────────────────────

static void handleRoot();
static void handleStatus();
static void handleConfigWifi();
static void handleSaveWifi();
static void handleConfigTime();
static void handleSaveTime();
static void handleConfigLocation();
static void handleSaveLocation();
static void handleConfigHardware();
static void handleSaveHardware();
static void handleConfigWatering();
static void handleNotFound();

// ─── Register ─────────────────────────────────────────────────────────────────

void registerHandlers(WebServerManager* wsm, Application* app) {
    g_wsm    = wsm;
    g_app    = app;
    g_server = wsm->getServer();

    g_server->on("/",                HTTP_GET,  handleRoot);
    g_server->on("/status",          HTTP_GET,  handleStatus);
    g_server->on("/config_wifi",     HTTP_GET,  handleConfigWifi);
    g_server->on("/save_wifi",       HTTP_POST, handleSaveWifi);
    g_server->on("/config_time",     HTTP_GET,  handleConfigTime);
    g_server->on("/save_time",       HTTP_POST, handleSaveTime);
    g_server->on("/config_location", HTTP_GET,  handleConfigLocation);
    g_server->on("/save_location",   HTTP_POST, handleSaveLocation);
    g_server->on("/config_hardware", HTTP_GET,  handleConfigHardware);
    g_server->on("/save_hardware",   HTTP_POST, handleSaveHardware);
    g_server->on("/config_watering", HTTP_GET,  handleConfigWatering);
    g_server->onNotFound(handleNotFound);

    Serial.println("[Web] Routes registered.");
}

void flushAndRestart(WebServerManager* /*wsm*/, int delayMs) {
    delay(delayMs);
    ESP.restart();
}

// ─── Handlers ─────────────────────────────────────────────────────────────────

static void handleRoot() {
    // Redirect to status page
    g_server->sendHeader("Location", "/status", true);
    g_server->send(302, "text/plain", "");
}

static void handleStatus() {
    ConfigManager*  cfg    = g_app->getConfigManager();
    WifiManager*    wifi   = g_app->getWifiManager();
    TimeSync*       ts     = g_app->getTimeSync();
    Ds3231Manager*  ds     = g_app->getDs3231();
    StateManager*   sm     = g_app->getStateManager();

    String page = buildPage(HTML_STATUS_PAGE);

    // Warnings
    page = replaceToken(page, "{ds3231_warning}",          ds3231WarningHtml());
    page = replaceToken(page, "{watering_locked_warning}", wateringLockedWarningHtml());

    // State
    page = replaceToken(page, "{state_str}",    String(sm->getStateString()));

    // WiFi
    String wifiStatus;
    if (wifi->isApModeActive()) {
        wifiStatus = "AP-Modus (Setup)";
    } else if (wifi->isConnected()) {
        wifiStatus = "Verbunden ✅";
    } else {
        wifiStatus = "Nicht verbunden ❌";
    }
    page = replaceToken(page, "{wifi_status}", wifiStatus);
    page = replaceToken(page, "{ip_address}",  wifi->isConnected() ? wifi->getLocalIP() : (wifi->isApModeActive() ? "192.168.4.1" : "–"));

    // Time
    String timeStr = "–";
    if (ts && ts->isSynced()) {
        timeStr = ts->getLocalTimeString();
    } else {
        // Fallback to system clock – use localtime_r() which respects the TZ set by TimeSync
        time_t now_t;
        time(&now_t);
        if (now_t > 1000000L) {
            struct tm t_buf;
            struct tm* t = localtime_r(&now_t, &t_buf);
            char buf[20];
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
            timeStr = String(buf);
        }
    }
    page = replaceToken(page, "{time_str}", timeStr);
    page = replaceToken(page, "{uptime}",   formatUptime());

    // DS3231
    if (ds && ds->isPresent()) {
        char tempBuf[16];
        snprintf(tempBuf, sizeof(tempBuf), "%.1f °C", ds->getTemperature());
        page = replaceToken(page, "{ds3231_status}", "OK ✅");
        page = replaceToken(page, "{rtc_temp}",      String(tempBuf));
    } else {
        page = replaceToken(page, "{ds3231_status}", "NICHT ERKANNT ⚠️");
        page = replaceToken(page, "{rtc_temp}",      "–");
    }

    // Offline mode
    SystemState state = sm->getState();
    page = replaceToken(page, "{offline_mode}",
        (state == SystemState::RUNNING_OFFLINE) ? "Ja" : "Nein");

    g_server->send(200, "text/html; charset=UTF-8", page);
    Serial.println("[Web] GET /status");
}

static void handleConfigWifi() {
    DeviceConfig& cfg = g_app->getConfigManager()->getDeviceConfig();
    String page = buildPage(HTML_WIFI_PAGE);
    page = replaceToken(page, "{ssid}",     String(cfg.ssid));
    page = replaceToken(page, "{password}", String(cfg.password));
    page = replaceToken(page, "{hostname}", String(cfg.hostname));
    g_server->send(200, "text/html; charset=UTF-8", page);
    Serial.println("[Web] GET /config_wifi");
}

static void handleSaveWifi() {
    if (g_server->method() != HTTP_POST) {
        g_server->send(405, "text/plain", "Method Not Allowed");
        return;
    }
    DeviceConfig& cfg = g_app->getConfigManager()->getDeviceConfig();
    if (g_server->hasArg("ssid"))     strlcpy(cfg.ssid,     g_server->arg("ssid").c_str(),     sizeof(cfg.ssid));
    if (g_server->hasArg("password")) strlcpy(cfg.password, g_server->arg("password").c_str(), sizeof(cfg.password));
    if (g_server->hasArg("hostname")) strlcpy(cfg.hostname, g_server->arg("hostname").c_str(), sizeof(cfg.hostname));
    g_app->getConfigManager()->saveDeviceConfig();

    String page = buildPage(HTML_SAVED_RESTART);
    g_server->send(200, "text/html; charset=UTF-8", page);
    Serial.println("[Web] POST /save_wifi – restarting...");
    g_app->scheduleRestart(2000);
}

static void handleConfigTime() {
    DeviceConfig& cfg = g_app->getConfigManager()->getDeviceConfig();
    String page = buildPage(HTML_TIME_PAGE);

    // Mark selected timezone
    struct { const char* val; const char* token; } tzList[] = {
        {"UTC",                              "{tz_UTC}"},
        {"CET-1CEST,M3.5.0,M10.5.0/3",     "{tz_CET}"},
        {"GMT0BST,M3.5.0/1,M10.5.0",        "{tz_GMT}"},
        {"EET-2EEST,M3.5.0/3,M10.5.0/4",   "{tz_EET}"},
        {"WET0WEST,M3.5.0/1,M10.5.0",       "{tz_WET}"},
        {"EST5EDT,M3.2.0,M11.1.0",           "{tz_EST}"},
        {"CST6CDT,M3.2.0,M11.1.0",           "{tz_CST}"},
        {"MST7MDT,M3.2.0,M11.1.0",           "{tz_MST}"},
        {"PST8PDT,M3.2.0,M11.1.0",           "{tz_PST}"},
        {"AEST-10AEDT,M10.1.0,M4.1.0/3",    "{tz_AEST}"},
        {"JST-9",                             "{tz_JST}"},
        {"CST-8",                             "{tz_CST8}"},
        {"IST-5:30",                          "{tz_IST}"},
        {nullptr, nullptr}
    };
    for (int i = 0; tzList[i].val; i++) {
        String sel = (strcmp(cfg.timezone, tzList[i].val) == 0) ? "selected" : "";
        page = replaceToken(page, tzList[i].token, sel);
    }
    page = replaceToken(page, "{ntpServer}", String(cfg.ntpServer));
    g_server->send(200, "text/html; charset=UTF-8", page);
    Serial.println("[Web] GET /config_time");
}

static void handleSaveTime() {
    if (g_server->method() != HTTP_POST) {
        g_server->send(405, "text/plain", "Method Not Allowed");
        return;
    }
    DeviceConfig& cfg = g_app->getConfigManager()->getDeviceConfig();
    if (g_server->hasArg("timezone"))  strlcpy(cfg.timezone,  g_server->arg("timezone").c_str(),  sizeof(cfg.timezone));
    if (g_server->hasArg("ntpServer")) strlcpy(cfg.ntpServer, g_server->arg("ntpServer").c_str(), sizeof(cfg.ntpServer));
    g_app->getConfigManager()->saveDeviceConfig();
    g_app->requestConfigApply();  // Live apply timezone change

    String page = buildPage(HTML_SAVED_LIVE);
    g_server->send(200, "text/html; charset=UTF-8", page);
    Serial.println("[Web] POST /save_time – live applied.");
}

static void handleConfigLocation() {
    DeviceConfig& cfg = g_app->getConfigManager()->getDeviceConfig();
    String page = buildPage(HTML_LOCATION_PAGE);
    char latBuf[16], lonBuf[16];
    snprintf(latBuf, sizeof(latBuf), "%.6f", cfg.latitude);
    snprintf(lonBuf, sizeof(lonBuf), "%.6f", cfg.longitude);
    page = replaceToken(page, "{latitude}",     String(latBuf));
    page = replaceToken(page, "{longitude}",    String(lonBuf));
    page = replaceToken(page, "{locationName}", String(cfg.locationName));
    g_server->send(200, "text/html; charset=UTF-8", page);
    Serial.println("[Web] GET /config_location");
}

static void handleSaveLocation() {
    if (g_server->method() != HTTP_POST) {
        g_server->send(405, "text/plain", "Method Not Allowed");
        return;
    }
    DeviceConfig& cfg = g_app->getConfigManager()->getDeviceConfig();
    if (g_server->hasArg("latitude"))     cfg.latitude  = g_server->arg("latitude").toFloat();
    if (g_server->hasArg("longitude"))    cfg.longitude = g_server->arg("longitude").toFloat();
    if (g_server->hasArg("locationName")) strlcpy(cfg.locationName, g_server->arg("locationName").c_str(), sizeof(cfg.locationName));
    g_app->getConfigManager()->saveDeviceConfig();

    String page = buildPage(HTML_SAVED_LIVE);
    g_server->send(200, "text/html; charset=UTF-8", page);
    Serial.println("[Web] POST /save_location – live applied.");
}

static void handleConfigHardware() {
    HardwareConfig& hw = g_app->getConfigManager()->getHardwareConfig();
    String page = buildPage(HTML_HARDWARE_PAGE);
    page = replaceToken(page, "{relayCount}", String(hw.relayCount));

    // Build dynamic pin input fields
    String pinHtml;
    for (int i = 0; i < hw.relayCount; i++) {
        char buf[128];
        snprintf(buf, sizeof(buf),
            "<label>Relais %d GPIO-Pin</label>"
            "<input type=\"number\" name=\"pin%d\" value=\"%d\" min=\"-1\" max=\"39\" "
            "placeholder=\"-1 = nicht belegt\">",
            i + 1, i, hw.relayPins[i]);
        pinHtml += buf;
    }
    page = replaceToken(page, "{relay_pins_html}", pinHtml);
    page = replaceToken(page, "{relay_inverted_checked}", hw.relayInverted ? "checked" : "");
    g_server->send(200, "text/html; charset=UTF-8", page);
    Serial.println("[Web] GET /config_hardware");
}

static void handleSaveHardware() {
    if (g_server->method() != HTTP_POST) {
        g_server->send(405, "text/plain", "Method Not Allowed");
        return;
    }
    HardwareConfig& hw = g_app->getConfigManager()->getHardwareConfig();
    HardwareConfig oldHw = hw;  // Keep copy for change detection

    if (g_server->hasArg("relayCount")) {
        hw.relayCount = constrain(g_server->arg("relayCount").toInt(), 0, MAX_RELAY_COUNT);
    }
    for (int i = 0; i < MAX_RELAY_COUNT; i++) {
        char key[8];
        snprintf(key, sizeof(key), "pin%d", i);
        if (g_server->hasArg(key)) {
            hw.relayPins[i] = g_server->arg(key).toInt();
        } else if (i >= hw.relayCount) {
            hw.relayPins[i] = -1;
        }
    }
    hw.relayInverted = g_server->hasArg("relayInverted");
    g_app->getConfigManager()->saveHardwareConfig();

    // Check if a restart is needed (relay count or pins changed)
    bool pinChanged = (oldHw.relayCount != hw.relayCount || oldHw.relayInverted != hw.relayInverted);
    if (!pinChanged) {
        for (int i = 0; i < MAX_RELAY_COUNT; i++) {
            if (oldHw.relayPins[i] != hw.relayPins[i]) { pinChanged = true; break; }
        }
    }

    if (pinChanged) {
        String page = buildPage(HTML_SAVED_RESTART);
        g_server->send(200, "text/html; charset=UTF-8", page);
        Serial.println("[Web] POST /save_hardware – restart required.");
        g_app->scheduleRestart(2000);
    } else {
        g_app->requestConfigApply();
        String page = buildPage(HTML_SAVED_LIVE);
        g_server->send(200, "text/html; charset=UTF-8", page);
        Serial.println("[Web] POST /save_hardware – live applied.");
    }
}

static void handleConfigWatering() {
    ConfigManager* cfg = g_app->getConfigManager();
    String page = buildPage(HTML_WATERING_PAGE);

    String wateringStatus;
    if (cfg->isWateringConfigValid()) {
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "<p style='color:#1a6b3c;margin-top:12px'>"
                 "✅ %d Einträge konfiguriert, %d Relais verfügbar.</p>",
                 cfg->getWateringConfig().count,
                 cfg->getHardwareConfig().relayCount);
        wateringStatus = buf;
    } else {
        wateringStatus = "<p style='color:#dc3545;margin-top:12px'>"
                         "❌ Kein gültiger Plan: Hardware oder Einträge fehlen.</p>";
    }
    page = replaceToken(page, "{watering_status}", wateringStatus);
    g_server->send(200, "text/html; charset=UTF-8", page);
    Serial.println("[Web] GET /config_watering");
}

static void handleNotFound() {
    // Captive portal redirect in AP mode
    if (g_app->isApModeActive()) {
        g_server->sendHeader("Location", "/", true);
        g_server->send(302, "text/plain", "");
        return;
    }
    String page = buildPage(HTML_404_PAGE);
    g_server->send(404, "text/html; charset=UTF-8", page);
}
