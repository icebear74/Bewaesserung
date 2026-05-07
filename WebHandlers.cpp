#include "WebHandlers.h"
#include "WebServerManager.h"
#include "WebPages.h"
#include "Application.h"
#include "ConfigManager.h"
#include "StateManager.h"
#include "WifiManager.h"
#include "TimeSync.h"
#include "Ds3231Manager.h"
#include "RelayManager.h"
#include "WeatherManager.h"
#include "WateringScheduler.h"
#include <WebServer.h>
#include <WiFi.h>
#include <ArduinoJson.h>

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
static void handleRelayTest();
static void handleConfigWatering();
static void handleSaveWatering();
static void handleApiWeather();
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
    g_server->on("/relay_test",      HTTP_POST, handleRelayTest);
    g_server->on("/config_watering", HTTP_GET,  handleConfigWatering);
    g_server->on("/save_watering",   HTTP_POST, handleSaveWatering);
    g_server->on("/api/weather",     HTTP_GET,  handleApiWeather);
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
    ConfigManager*     cfg    = g_app->getConfigManager();
    WifiManager*       wifi   = g_app->getWifiManager();
    TimeSync*          ts     = g_app->getTimeSync();
    Ds3231Manager*     ds     = g_app->getDs3231();
    StateManager*      sm     = g_app->getStateManager();
    WeatherManager*    wm     = g_app->getWeatherManager();
    WateringScheduler* sched  = g_app->getScheduler();

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

    // ── Pump status ───────────────────────────────────────────────────────────
    HardwareConfig& hw = cfg->getHardwareConfig();
    String pumpHtml;
    if (hw.relayCount > 0 && sched) {
        pumpHtml += "<h2 style='margin-top:4px;color:#1a6b3c'>💧 Pumpen</h2>";
        pumpHtml += "<table><tr><th>Pumpe</th><th>Status</th></tr>";
        RelayManager* rm = g_app->getRelayManager();
        for (int i = 0; i < hw.relayCount; i++) {
            const PumpEntry& p = hw.pumps[i];
            String name = p.name[0] ? String(p.name) : ("Pumpe " + String(i + 1));
            bool active = rm && rm->isActive(i);
            String status = active ? "<span style='color:#dc3545;font-weight:bold'>EIN 🔴</span>"
                                   : "<span style='color:#1a6b3c'>AUS</span>";
            pumpHtml += "<tr><td>" + name + "</td><td>" + status + "</td></tr>";
        }
        pumpHtml += "</table>";
    } else {
        pumpHtml = "<p style='color:#999;font-style:italic'>Keine Pumpen konfiguriert.</p>";
    }
    page = replaceToken(page, "{pump_status_html}", pumpHtml);

    // ── Weather section ───────────────────────────────────────────────────────
    String weatherHtml;
    if (wm && wm->isAvailable()) {
        const WeatherData& w = wm->getData();
        bool stale = wm->isStale();
        char buf[256];

        weatherHtml += "<div style='margin-top:16px;border-top:1px solid #eee;padding-top:12px'>";
        String locName = String(cfg->getDeviceConfig().locationName);
        if (locName.isEmpty()) locName = "Aktueller Standort";
        weatherHtml += "<h2 style='color:#1a6b3c'>🌤️ Wetter – " + locName + "</h2>";
        if (stale) {
            weatherHtml += "<div class='alert-warning' style='margin-bottom:8px'>⚠️ Wetterdaten veraltet (kein Internet?)</div>";
        }
        weatherHtml += "<div style='display:flex;gap:16px;flex-wrap:wrap'>";
        // Left column
        weatherHtml += "<div style='flex:1;min-width:200px'><table>";
        snprintf(buf, sizeof(buf), "%.1f °C (Gefühlt: %.1f °C)", w.temperature, w.feelsLike);
        weatherHtml += "<tr><td>Temperatur</td><td>" + String(buf) + "</td></tr>";
        snprintf(buf, sizeof(buf), "%.0f %%", w.humidity);
        weatherHtml += "<tr><td>Luftfeuchte</td><td>" + String(buf) + "</td></tr>";
        snprintf(buf, sizeof(buf), "%.1f mm (%.0f %%)", w.dailyPrecipMm, w.dailyPrecipPct);
        weatherHtml += "<tr><td>Niederschlag heute</td><td>" + String(buf) + "</td></tr>";
        snprintf(buf, sizeof(buf), "%.1f mm", w.rain);
        weatherHtml += "<tr><td>Regen aktuell</td><td>" + String(buf) + "</td></tr>";
        if (w.snow > 0.0f) {
            snprintf(buf, sizeof(buf), "%.1f mm", w.snow);
            weatherHtml += "<tr><td>Schnee</td><td>" + String(buf) + "</td></tr>";
        }
        snprintf(buf, sizeof(buf), "%.1f km/h (%.0f°)", w.windSpeed, w.windDir);
        weatherHtml += "<tr><td>Wind</td><td>" + String(buf) + "</td></tr>";
        snprintf(buf, sizeof(buf), "%.1f / %.1f °C", w.tempMin, w.tempMax);
        weatherHtml += "<tr><td>Min/Max heute</td><td>" + String(buf) + "</td></tr>";
        weatherHtml += "</table></div>";
        // Right column: astronomical + update time
        weatherHtml += "<div style='flex:1;min-width:200px'><table>";
        if (w.sunrise > 0) {
            struct tm sr, ss;
            localtime_r(&w.sunrise, &sr);
            localtime_r(&w.sunset,  &ss);
            snprintf(buf, sizeof(buf), "%02d:%02d", sr.tm_hour, sr.tm_min);
            weatherHtml += "<tr><td>Sonnenaufgang</td><td>" + String(buf) + "</td></tr>";
            snprintf(buf, sizeof(buf), "%02d:%02d", ss.tm_hour, ss.tm_min);
            weatherHtml += "<tr><td>Sonnenuntergang</td><td>" + String(buf) + "</td></tr>";
        }
        if (w.lastUpdate > 0) {
            struct tm lu;
            localtime_r(&w.lastUpdate, &lu);
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                     lu.tm_hour, lu.tm_min, lu.tm_sec);
            String age = stale ? " <span style='color:#dc3545'>(veraltet)</span>"
                               : " <span style='color:#1a6b3c'>✅</span>";
            weatherHtml += "<tr><td>Letztes Update</td><td>" + String(buf) + age + "</td></tr>";
        }
        weatherHtml += "</table></div>";
        weatherHtml += "</div></div>";  // flex + outer div
    } else if (wm) {
        weatherHtml = "<div style='margin-top:16px;border-top:1px solid #eee;padding-top:12px'>"
                      "<p style='color:#999'>🌤️ Keine Wetterdaten verfügbar (Internetverbindung erforderlich).</p>"
                      "<button class='btn' style='margin-top:8px;padding:6px 14px;font-size:13px' "
                      "onclick=\"fetch('/api/weather?refresh=1').then(()=>location.reload())\">🔄 Wetter aktualisieren</button>"
                      "</div>";
    }
    page = replaceToken(page, "{weather_html}", weatherHtml);

    g_server->send(200, "text/html; charset=UTF-8", page);
    Serial.println("[Web] GET /status");
}

static void handleConfigWifi() {
    DeviceConfig& cfg = g_app->getConfigManager()->getDeviceConfig();
    String page = buildPage(HTML_WIFI_PAGE);
    page = replaceToken(page, "{ssid}",     String(cfg.ssid));
    page = replaceToken(page, "{hostname}", String(cfg.hostname));
    // First setup: password is empty – show field as plain text so user can read what they type.
    // After initial setup: field is empty (no pre-fill) so current password is never exposed.
    bool firstSetup = (cfg.password[0] == '\0');
    String pwField;
    if (firstSetup) {
        pwField = "<input type=\"text\" id=\"password\" name=\"password\" "
                  "placeholder=\"Passwort festlegen\" maxlength=\"63\">";
    } else {
        pwField = "<input type=\"password\" id=\"password\" name=\"password\" "
                  "placeholder=\"Leer lassen = keine Änderung\" maxlength=\"63\">";
    }
    page = replaceToken(page, "{password_field}", pwField);
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
    // Only update password if a non-empty value was submitted
    if (g_server->hasArg("password") && !g_server->arg("password").isEmpty()) {
        strlcpy(cfg.password, g_server->arg("password").c_str(), sizeof(cfg.password));
    }
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
    page = replaceToken(page, "{saved_back_url}", "/config_time");
    g_server->send(200, "text/html; charset=UTF-8", page);
    Serial.println("[Web] POST /save_time – live applied.");
}

static void handleConfigLocation() {
    DeviceConfig& cfg = g_app->getConfigManager()->getDeviceConfig();
    String page = buildPage(HTML_LOCATION_PAGE);
    char latBuf[16], lonBuf[16];
    // Use 4 decimal places (~11m precision) matching the map JS toFixed(4)
    snprintf(latBuf, sizeof(latBuf), "%.4f", cfg.latitude);
    snprintf(lonBuf, sizeof(lonBuf), "%.4f", cfg.longitude);
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
    page = replaceToken(page, "{saved_back_url}", "/config_location");
    g_server->send(200, "text/html; charset=UTF-8", page);
    Serial.println("[Web] POST /save_location – live applied.");
}

// ─── Helper: build expander (optional hardware) HTML row ─────────────────────

static String buildExpanderRowHtml(int i, const ExpanderEntry& e) {
    String r;
    r.reserve(600);
    r += "<div class=\"pump-entry\" style=\"border:1px solid #cce0ff;padding:10px;margin-bottom:8px;border-radius:4px;background:#f5f9ff\">";
    r += "<b>Expander "; r += (i + 1); r += "</b>";
    r += "<div class=\"form-row\" style=\"margin-top:6px\">";
    r += "<div class=\"form-col\"><label><input type=\"checkbox\" name=\"ex"; r += i; r += "_enabled\"";
    if (e.enabled) r += " checked";
    r += "> Aktiv</label></div>";
    r += "<div class=\"form-col\"><label>Name</label><input type=\"text\" name=\"ex"; r += i;
    r += "_name\" value=\""; r += String(e.name); r += "\" maxlength=\"31\"></div></div>";
    r += "<div class=\"form-row\">";
    r += "<div class=\"form-col\"><label>Chiptyp</label><select name=\"ex"; r += i; r += "_type\">";
    r += "<option value=\"0\""; if (e.chipType == EXPANDER_TYPE_PCF8574) r += " selected";
    r += ">PCF8574 (8 Ports, 0x20&#x2013;0x27)</option>";
    r += "<option value=\"1\""; if (e.chipType == EXPANDER_TYPE_PCF8575) r += " selected";
    r += ">PCF8575 (16 Ports, 0x20&#x2013;0x27)</option>";
    r += "</select></div>";
    r += "<div class=\"form-col\"><label>I2C-Adresse (dez., 32=0x20 &#x2026; 39=0x27)</label>";
    r += "<input type=\"number\" name=\"ex"; r += i; r += "_addr\" value=\"";
    r += e.i2cAddress; r += "\" min=\"32\" max=\"39\"></div></div>";
    r += "</div>";
    return r;
}

// ─── Helper: build per-pump HTML row ─────────────────────────────────────────

static String buildPumpRowHtml(int i, const PumpEntry& p, const HardwareConfig& hw) {
    String r;
    r.reserve(1000);
    r += "<div class=\"pump-entry\" id=\"prow"; r += i; r += "\" style=\"border:1px solid #ddd;padding:10px;margin-bottom:10px;border-radius:4px\">";
    r += "<div style=\"display:flex;justify-content:space-between;align-items:center;margin-bottom:6px\">";
    r += "<b>Pumpe #"; r += (i + 1); r += "</b>";
    r += "<button type=\"button\" onclick=\"deletePump("; r += i; r += ")\" ";
    r += "style=\"padding:3px 10px;background:#dc3545;color:#fff;border:none;border-radius:4px;cursor:pointer\">&#10005; L&#246;schen</button>";
    r += "</div>";
    r += "<div class=\"form-row\">";
    r += "<div class=\"form-col\"><label><input type=\"checkbox\" name=\"p"; r += i; r += "_enabled\"";
    if (p.enabled) r += " checked";
    r += "> Aktiv</label></div>";
    r += "<div class=\"form-col\"><label>Name</label><input type=\"text\" name=\"p"; r += i;
    r += "_name\" value=\""; r += String(p.name); r += "\" maxlength=\"31\"></div></div>";
    // Output type selector
    r += "<div class=\"form-row\">";
    r += "<div class=\"form-col\"><label>Ausgangstyp</label><select name=\"p"; r += i;
    r += "_type\" onchange=\"toggleOutType("; r += i; r += ",this.value)\">";
    r += "<option value=\"0\""; if (p.outputType == OUTPUT_TYPE_GPIO)    r += " selected"; r += ">Direkt-GPIO</option>";
    r += "<option value=\"1\""; if (p.outputType == OUTPUT_TYPE_PCF8574) r += " selected"; r += ">I2C Expander (PCF8574/8575)</option>";
    r += "</select></div></div>";
    // GPIO-specific fields
    bool isI2C = (p.outputType == OUTPUT_TYPE_PCF8574);
    r += "<div id=\"gpio"; r += i; r += "\" style=\"display:"; r += (isI2C ? "none" : "block"); r += "\">";
    r += "<div class=\"form-row\"><div class=\"form-col\"><label>GPIO-Pin (-1 = inaktiv)</label>";
    r += "<input type=\"number\" name=\"p"; r += i; r += "_pin\" value=\"";
    r += p.pin; r += "\" min=\"-1\" max=\"39\"></div></div></div>";
    // I2C-specific fields: expander dropdown + channel
    r += "<div id=\"i2c"; r += i; r += "\" style=\"display:"; r += (isI2C ? "block" : "none"); r += "\">";
    r += "<div class=\"form-row\">";
    r += "<div class=\"form-col\"><label>Expander</label><select name=\"p"; r += i;
    r += "_expander\" onchange=\"onExpanderChange("; r += i; r += ",this)\">";
    if (hw.expanderCount == 0) {
        r += "<option value=\"0\" disabled>&#x26A0; Kein Expander definiert &#x2013; erst Expander anlegen</option>";
    } else {
        for (int e = 0; e < hw.expanderCount; e++) {
            char hexBuf[8];
            snprintf(hexBuf, sizeof(hexBuf), "0x%02X", hw.expanders[e].i2cAddress);
            r += "<option value=\""; r += e; r += "\"";
            if (p.expanderIndex == (uint8_t)e) r += " selected";
            r += ">"; r += String(hw.expanders[e].name); r += " (";
            r += hexBuf; r += ", ";
            r += (hw.expanders[e].chipType == EXPANDER_TYPE_PCF8575) ? "PCF8575" : "PCF8574";
            r += ")</option>";
        }
    }
    r += "</select></div>";
    uint8_t maxChan = 7;
    if (p.expanderIndex < (uint8_t)hw.expanderCount &&
        hw.expanders[p.expanderIndex].chipType == EXPANDER_TYPE_PCF8575) {
        maxChan = 15;
    }
    r += "<div class=\"form-col\"><label>Kanal (0&#x2013;"; r += maxChan; r += ")</label>";
    r += "<input type=\"number\" name=\"p"; r += i; r += "_i2cChan\" id=\"chan"; r += i;
    r += "\" value=\""; r += p.i2cChannel; r += "\" min=\"0\" max=\""; r += maxChan; r += "\"></div>";
    r += "</div></div>";
    // Common fields
    r += "<div class=\"form-row\" style=\"margin-top:4px\">";
    r += "<div class=\"form-col\"><label><input type=\"checkbox\" name=\"p"; r += i; r += "_invert\"";
    if (p.invertLogic) r += " checked";
    r += "> Aktiv-LOW (invertiert)</label></div>";
    r += "<div class=\"form-col\"><label>Max. Test-Laufzeit (s)</label>";
    r += "<input type=\"number\" name=\"p"; r += i; r += "_maxRuntime\" value=\"";
    r += p.maxRuntimeSec; r += "\" min=\"1\" max=\"3600\"></div></div>";
    r += "<label>Notizen</label><input type=\"text\" name=\"p"; r += i; r += "_notes\" value=\"";
    r += String(p.notes); r += "\" maxlength=\"63\">";
    r += "<div style=\"margin-top:8px\">";
    r += "<button type=\"button\" onclick=\"testRelay("; r += i; r += ",'on')\" ";
    r += "style=\"margin-right:4px;padding:5px 14px;background:#1a6b3c;color:#fff;border:none;border-radius:4px;cursor:pointer\">&#9654; Test EIN</button>";
    r += "<button type=\"button\" onclick=\"testRelay("; r += i; r += ",'off')\" ";
    r += "style=\"padding:5px 14px;background:#dc3545;color:#fff;border:none;border-radius:4px;cursor:pointer\">&#9646; Test AUS</button>";
    r += " <span id=\"ts"; r += i; r += "\" style=\"font-size:12px;color:#666\"></span>";
    r += "</div></div>";
    return r;
}

static void handleConfigHardware() {
    HardwareConfig& hw = g_app->getConfigManager()->getHardwareConfig();
    String page = buildPage(HTML_HARDWARE_PAGE);

    // ── Expander section ──────────────────────────────────────────────────────
    page = replaceToken(page, "{expanderCount}", String(hw.expanderCount));
    String expanderRowsHtml;
    for (int i = 0; i < hw.expanderCount; i++) {
        expanderRowsHtml += buildExpanderRowHtml(i, hw.expanders[i]);
    }
    page = replaceToken(page, "{expander_rows_html}", expanderRowsHtml);

    // Build JSON array of expanders for use in the JS pump dropdown
    String expandersJson = "[";
    for (int i = 0; i < hw.expanderCount; i++) {
        if (i > 0) expandersJson += ",";
        char hexBuf[8];
        snprintf(hexBuf, sizeof(hexBuf), "0x%02X", hw.expanders[i].i2cAddress);
        // Escape any double-quotes in the expander name
        String safeName = String(hw.expanders[i].name);
        safeName.replace("\"", "&quot;");
        expandersJson += "{\"name\":\""; expandersJson += safeName; expandersJson += "\"";
        expandersJson += ",\"chipType\":"; expandersJson += hw.expanders[i].chipType;
        expandersJson += ",\"maxChan\":"; expandersJson += (hw.expanders[i].chipType == EXPANDER_TYPE_PCF8575 ? 15 : 7);
        expandersJson += ",\"addr\":\""; expandersJson += hexBuf; expandersJson += "\"}";
    }
    expandersJson += "]";
    page = replaceToken(page, "{expanders_json}", expandersJson);

    // ── Pump section ─────────────────────────────────────────────────────────
    page = replaceToken(page, "{pumpCount}", String(hw.relayCount));
    String pumpRowsHtml;
    for (int i = 0; i < hw.relayCount; i++) {
        pumpRowsHtml += buildPumpRowHtml(i, hw.pumps[i], hw);
    }
    page = replaceToken(page, "{pump_rows_html}", pumpRowsHtml);
    // Show "no pumps" message when there are no pumps configured
    page = replaceToken(page, "{noPumpsMsg}",
        hw.relayCount == 0 ? "block" : "none");
    g_server->send(200, "text/html; charset=UTF-8", page);
    Serial.println("[Web] GET /config_hardware");
}

static void handleSaveHardware() {
    if (g_server->method() != HTTP_POST) {
        g_server->send(405, "text/plain", "Method Not Allowed");
        return;
    }
    HardwareConfig newHw;

    // ── Parse expanders (optional hardware) first ─────────────────────────────
    newHw.expanderCount = constrain(
        g_server->hasArg("expCount") ? g_server->arg("expCount").toInt() : 0,
        0, MAX_EXPANDER_COUNT);
    for (int i = 0; i < MAX_EXPANDER_COUNT; i++) {
        ExpanderEntry& e = newHw.expanders[i];
        if (i < newHw.expanderCount) {
            char key[24];
            snprintf(key, sizeof(key), "ex%d_enabled", i); e.enabled    = g_server->hasArg(key);
            snprintf(key, sizeof(key), "ex%d_type", i);    e.chipType   = (uint8_t)constrain(g_server->hasArg(key) ? g_server->arg(key).toInt() : 0, 0, 1);
            snprintf(key, sizeof(key), "ex%d_addr", i);    e.i2cAddress = (uint8_t)constrain(g_server->hasArg(key) ? g_server->arg(key).toInt() : 0x20, 0x20, 0x27);
            snprintf(key, sizeof(key), "ex%d_name", i);
            if (g_server->hasArg(key)) strlcpy(e.name, g_server->arg(key).c_str(), sizeof(e.name));
        }
    }

    // ── Parse pumps ───────────────────────────────────────────────────────────
    // pumpCount holds the highest allocated pump index (from JS _nextPumpIdx).
    // Deleted pumps have no form fields; we skip gaps by checking for p{i}_name presence.
    // We scan up to PUMP_SCAN_FACTOR times the maximum pump count to safely handle
    // sessions with many repeated add/delete cycles (JS never resets _nextPumpIdx).
    static const int PUMP_SCAN_FACTOR = 8;
    int pumpScanLimit = constrain(
        g_server->hasArg("pumpCount") ? g_server->arg("pumpCount").toInt() : 0,
        0, MAX_RELAY_COUNT * PUMP_SCAN_FACTOR);

    newHw.relayCount = 0;
    for (int i = 0; i < pumpScanLimit && newHw.relayCount < MAX_RELAY_COUNT; i++) {
        char key[24];
        snprintf(key, sizeof(key), "p%d_name", i);
        if (!g_server->hasArg(key)) continue;  // pump was deleted – skip this index

        PumpEntry& p = newHw.pumps[newHw.relayCount];
        snprintf(key, sizeof(key), "p%d_enabled", i);   p.enabled       = g_server->hasArg(key);
        snprintf(key, sizeof(key), "p%d_type", i);      p.outputType    = (uint8_t)constrain(g_server->hasArg(key) ? g_server->arg(key).toInt() : 0, 0, 1);
        snprintf(key, sizeof(key), "p%d_pin", i);       p.pin           = g_server->hasArg(key) ? g_server->arg(key).toInt() : -1;
        snprintf(key, sizeof(key), "p%d_expander", i);  p.expanderIndex = (uint8_t)constrain(g_server->hasArg(key) ? g_server->arg(key).toInt() : 0, 0, MAX_EXPANDER_COUNT - 1);
        snprintf(key, sizeof(key), "p%d_i2cChan", i);   p.i2cChannel    = (uint8_t)constrain(g_server->hasArg(key) ? g_server->arg(key).toInt() : 0, 0, 15);
        snprintf(key, sizeof(key), "p%d_invert", i);    p.invertLogic   = g_server->hasArg(key);
        snprintf(key, sizeof(key), "p%d_maxRuntime", i);p.maxRuntimeSec = g_server->hasArg(key) ? constrain(g_server->arg(key).toInt(), 1, 3600) : 300;
        snprintf(key, sizeof(key), "p%d_name", i);
        if (g_server->hasArg(key)) strlcpy(p.name, g_server->arg(key).c_str(), sizeof(p.name));
        snprintf(key, sizeof(key), "p%d_notes", i);
        if (g_server->hasArg(key)) strlcpy(p.notes, g_server->arg(key).c_str(), sizeof(p.notes));
        newHw.relayCount++;
    }

    // Validate: check for duplicate GPIO pins
    for (int i = 0; i < newHw.relayCount; i++) {
        const PumpEntry& pi = newHw.pumps[i];
        if (!pi.enabled || pi.outputType != OUTPUT_TYPE_GPIO || pi.pin < 0) continue;
        for (int j = i + 1; j < newHw.relayCount; j++) {
            const PumpEntry& pj = newHw.pumps[j];
            if (!pj.enabled || pj.outputType != OUTPUT_TYPE_GPIO || pj.pin < 0) continue;
            if (pi.pin == pj.pin) {
                String page = buildPage(HTML_ERROR_PAGE);
                page = replaceToken(page, "{error_msg}",
                    "Doppelte GPIO-Pin-Belegung: Pin " + String(pi.pin) +
                    " ist Pumpe " + String(i + 1) + " und Pumpe " + String(j + 1) + " zugewiesen.");
                page = replaceToken(page, "{back_url}", "/config_hardware");
                g_server->send(400, "text/html; charset=UTF-8", page);
                return;
            }
        }
    }
    // Validate: check GPIO pin range
    for (int i = 0; i < newHw.relayCount; i++) {
        const PumpEntry& p = newHw.pumps[i];
        if (!p.enabled || p.outputType != OUTPUT_TYPE_GPIO || p.pin < 0) continue;
        if (p.pin > 39) {
            String page = buildPage(HTML_ERROR_PAGE);
            page = replaceToken(page, "{error_msg}",
                "Ung&#252;ltiger GPIO-Pin " + String(p.pin) + " bei Pumpe " + String(i + 1) + " (g&#252;ltig: -1 oder 0-39).");
            page = replaceToken(page, "{back_url}", "/config_hardware");
            g_server->send(400, "text/html; charset=UTF-8", page);
            return;
        }
    }
    // Validate: check for duplicate I2C addresses among expanders
    for (int i = 0; i < newHw.expanderCount; i++) {
        if (!newHw.expanders[i].enabled) continue;
        for (int j = i + 1; j < newHw.expanderCount; j++) {
            if (!newHw.expanders[j].enabled) continue;
            if (newHw.expanders[i].i2cAddress == newHw.expanders[j].i2cAddress) {
                String page = buildPage(HTML_ERROR_PAGE);
                page = replaceToken(page, "{error_msg}",
                    "Doppelte I2C-Adresse 0x" + String(newHw.expanders[i].i2cAddress, HEX) +
                    " bei Expander " + String(i + 1) + " und Expander " + String(j + 1) + ".");
                page = replaceToken(page, "{back_url}", "/config_hardware");
                g_server->send(400, "text/html; charset=UTF-8", page);
                return;
            }
        }
    }
    // Validate: check for duplicate expander+channel combinations across pumps
    for (int i = 0; i < newHw.relayCount; i++) {
        const PumpEntry& pi = newHw.pumps[i];
        if (!pi.enabled || pi.outputType != OUTPUT_TYPE_PCF8574) continue;
        for (int j = i + 1; j < newHw.relayCount; j++) {
            const PumpEntry& pj = newHw.pumps[j];
            if (!pj.enabled || pj.outputType != OUTPUT_TYPE_PCF8574) continue;
            if (pi.expanderIndex == pj.expanderIndex && pi.i2cChannel == pj.i2cChannel) {
                String page = buildPage(HTML_ERROR_PAGE);
                page = replaceToken(page, "{error_msg}",
                    "Doppelte I2C-Kanal-Belegung: Expander " + String(pi.expanderIndex + 1) +
                    " Kanal " + String(pi.i2cChannel) +
                    " ist Pumpe " + String(i + 1) + " und Pumpe " + String(j + 1) + " zugewiesen.");
                page = replaceToken(page, "{back_url}", "/config_hardware");
                g_server->send(400, "text/html; charset=UTF-8", page);
                return;
            }
        }
    }
    // Validate: PCF pump's expanderIndex must be within configured expander count
    for (int i = 0; i < newHw.relayCount; i++) {
        const PumpEntry& p = newHw.pumps[i];
        if (!p.enabled || p.outputType != OUTPUT_TYPE_PCF8574) continue;
        if (p.expanderIndex >= (uint8_t)newHw.expanderCount) {
            String page = buildPage(HTML_ERROR_PAGE);
            page = replaceToken(page, "{error_msg}",
                "Pumpe " + String(i + 1) + " referenziert Expander " + String(p.expanderIndex + 1) +
                ", aber es sind nur " + String(newHw.expanderCount) + " Expander konfiguriert.");
            page = replaceToken(page, "{back_url}", "/config_hardware");
            g_server->send(400, "text/html; charset=UTF-8", page);
            return;
        }
        // Validate channel range against chip type
        uint8_t maxChan = (newHw.expanders[p.expanderIndex].chipType == EXPANDER_TYPE_PCF8575) ? 15 : 7;
        if (p.i2cChannel > maxChan) {
            String page = buildPage(HTML_ERROR_PAGE);
            page = replaceToken(page, "{error_msg}",
                "Pumpe " + String(i + 1) + ": Kanal " + String(p.i2cChannel) +
                " ist zu gross f&#252;r den gew&#228;hlten Expander (max. " + String(maxChan) + ").");
            page = replaceToken(page, "{back_url}", "/config_hardware");
            g_server->send(400, "text/html; charset=UTF-8", page);
            return;
        }
    }

    g_app->getConfigManager()->getHardwareConfig() = newHw;
    g_app->getConfigManager()->saveHardwareConfig();
    g_app->requestConfigApply();

    String page = buildPage(HTML_SAVED_LIVE);
    page = replaceToken(page, "{saved_back_url}", "/config_hardware");
    g_server->send(200, "text/html; charset=UTF-8", page);
    Serial.println("[Web] POST /save_hardware – live applied.");
}

static void handleRelayTest() {
    if (g_server->method() != HTTP_POST) {
        g_server->send(405, "text/plain", "Method Not Allowed");
        return;
    }
    int relay  = g_server->hasArg("relay")  ? g_server->arg("relay").toInt()  : -1;
    String act = g_server->hasArg("action") ? g_server->arg("action")         : "";

    RelayManager* rm = g_app->getRelayManager();
    if (!rm || relay < 0 || relay >= rm->getRelayCount()) {
        g_server->send(200, "application/json", "{\"ok\":false,\"msg\":\"Ung\\u00fcltiger Pumpenindex\"}");
        return;
    }
    bool ok = false;
    String msg;
    if (act == "on") {
        ok = rm->testActivateRelay(relay);
        if (ok) {
            HardwareConfig& hw = g_app->getConfigManager()->getHardwareConfig();
            int timeout = hw.pumps[relay].maxRuntimeSec > 0 ? hw.pumps[relay].maxRuntimeSec : 30;
            msg = "Pumpe " + String(relay + 1) + " EIN (Auto-AUS nach " + String(timeout) + "s)";
        } else {
            msg = "Pumpe " + String(relay + 1) + ": Aktivierung fehlgeschlagen (deaktiviert oder kein Pin)";
        }
    } else if (act == "off") {
        ok = rm->testDeactivateRelay(relay);
        msg = ok ? ("Pumpe " + String(relay + 1) + " AUS") : ("Pumpe " + String(relay + 1) + ": Fehler");
    } else {
        msg = "Unbekannte Aktion";
    }

    // Use ArduinoJson for correct escaping of all JSON special characters
    JsonDocument respDoc;
    respDoc["ok"]  = ok;
    respDoc["msg"] = msg;
    String json;
    serializeJson(respDoc, json);
    g_server->send(200, "application/json", json);
    Serial.printf("[Web] POST /relay_test relay=%d action=%s ok=%d\n", relay, act.c_str(), ok);
}

// ─── Helper: build watering slot HTML row (server-side) ──────────────────────

static String buildSlotRowHtml(int si, const WateringSlot& slot,
                                const SlotConfig& sc, const HardwareConfig& hw) {
    const char* dayLabels[] = {"Mo","Di","Mi","Do","Fr","Sa","So"};
    const char* trigLabels[] = {"Feste Uhrzeit","Sonnenaufgang","Sonnenuntergang",
                                 "Mittagszeit","Offset (relativ)"};
    const char* baseLabels[] = {"Sonnenaufgang","Sonnenuntergang","Mittagszeit"};
    String r;
    r.reserve(2000);
    r += "<div class=\"pump-entry\" id=\"slot"; r += si;
    r += "\" style=\"border:1px solid #b3d4b3;padding:12px;margin-bottom:12px;border-radius:6px;background:#f9fff9\">";
    // Header
    r += "<div style=\"display:flex;justify-content:space-between;align-items:center;margin-bottom:8px\">";
    r += "<b style=\"font-size:1.05em\">&#128337; Slot ";  r += (si + 1);
    if (slot.name[0]) { r += " &ndash; "; r += String(slot.name); }
    r += "</b>";
    r += "<button type=\"button\" onclick=\"deleteSlot("; r += si;
    r += ")\" style=\"padding:3px 10px;background:#dc3545;color:#fff;border:none;border-radius:4px;cursor:pointer\">&#10005; Slot l&#246;schen</button>";
    r += "</div>";
    // Enabled + Name row
    r += "<div class=\"form-row\">";
    r += "<div class=\"form-col\"><label><input type=\"checkbox\" name=\"s"; r += si; r += "_enabled\"";
    if (slot.enabled) r += " checked";
    r += "> Aktiv</label></div>";
    r += "<div class=\"form-col\"><label>Name</label><input type=\"text\" name=\"s"; r += si;
    r += "_name\" value=\""; r += String(slot.name); r += "\" maxlength=\"31\" required></div>";
    r += "</div>";
    // Trigger type + fixed time
    r += "<div class=\"form-row\">";
    r += "<div class=\"form-col\"><label>Ausl&ouml;ser</label><select name=\"s"; r += si;
    r += "_trigger\" onchange=\"onTriggerChange("; r += si; r += ",this.value)\">";
    for (int t = 0; t < 5; t++) {
        r += "<option value=\""; r += t; r += "\"";
        if (slot.triggerType == (uint8_t)t) r += " selected";
        r += ">"; r += trigLabels[t]; r += "</option>";
    }
    r += "</select></div>";
    char timeBuf[8];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", slot.fixedHour, slot.fixedMinute);
    r += "<div class=\"form-col\"><label>Uhrzeit / Fallback</label>";
    r += "<input type=\"time\" name=\"s"; r += si; r += "_time\" value=\""; r += timeBuf; r += "\"></div>";
    r += "</div>";
    // Offset fields (visible only when trigger=4)
    bool isOffset = (slot.triggerType == TRIGGER_OFFSET);
    r += "<div id=\"offsetRow"; r += si;
    r += "\" style=\"display:"; r += (isOffset ? "flex" : "none"); r += ";\" class=\"form-row\">";
    r += "<div class=\"form-col\"><label>Offset-Basis</label><select name=\"s"; r += si; r += "_offsetBase\">";
    for (int b = 0; b < 3; b++) {
        r += "<option value=\""; r += b; r += "\"";
        if (slot.offsetBase == (uint8_t)b) r += " selected";
        r += ">"; r += baseLabels[b]; r += "</option>";
    }
    r += "</select></div>";
    r += "<div class=\"form-col\"><label>Offset (Min., negativ = davor)</label>";
    r += "<input type=\"number\" name=\"s"; r += si; r += "_offsetMin\" value=\"";
    r += slot.offsetMinutes; r += "\" min=\"-720\" max=\"720\"></div>";
    r += "</div>";
    // Days
    r += "<div style=\"margin-top:6px\">";
    for (int d = 0; d < 7; d++) {
        r += "<label style=\"margin-right:7px\"><input type=\"checkbox\" name=\"s"; r += si;
        r += "_d"; r += d; r += "\"";
        if (slot.days & (1 << d)) r += " checked";
        r += "> "; r += dayLabels[d]; r += "</label>";
    }
    r += "</div>";
    // Weather conditions (collapsible)
    r += "<details style=\"margin-top:8px\"><summary style=\"cursor:pointer;font-weight:bold;color:#1a6b3c\">";
    r += "&#127777;&#65039; Wetterbedingungen</summary>";
    r += "<div class=\"form-row\" style=\"margin-top:6px\">";
    r += "<div class=\"form-col\"><label>Aussetzen wenn Regen &ge; (mm, 0=aus)</label>";
    r += "<input type=\"number\" name=\"s"; r += si; r += "_skipRainMm\" value=\"";
    r += slot.skipIfRainMm; r += "\" min=\"0\" max=\"100\" step=\"0.1\"></div>";
    r += "<div class=\"form-col\"><label>Aussetzen wenn Regenwahrsch. &ge; (%, 0=aus)</label>";
    r += "<input type=\"number\" name=\"s"; r += si; r += "_skipRainPct\" value=\"";
    r += slot.skipIfRainPct; r += "\" min=\"0\" max=\"100\"></div>";
    r += "</div><div class=\"form-row\">";
    r += "<div class=\"form-col\"><label>Nur wenn Temp. &ge; (°C, -99=immer)</label>";
    r += "<input type=\"number\" name=\"s"; r += si; r += "_aboveTemp\" value=\"";
    r += slot.runOnlyAboveTemp; r += "\" min=\"-99\" max=\"60\" step=\"0.5\"></div>";
    r += "<div class=\"form-col\"><label>Dauer reduzieren wenn Regen &ge; (mm, 0=aus)</label>";
    r += "<input type=\"number\" name=\"s"; r += si; r += "_reduceRainMm\" value=\"";
    r += slot.reduceIfRainMm; r += "\" min=\"0\" max=\"100\" step=\"0.1\"></div>";
    r += "</div><div class=\"form-row\">";
    r += "<div class=\"form-col\"><label>Dauer-Reduktion (%)</label>";
    r += "<input type=\"number\" name=\"s"; r += si; r += "_reducePct\" value=\"";
    r += slot.reducePct; r += "\" min=\"1\" max=\"99\"></div>";
    r += "</div></details>";
    // Pump assignments for this slot
    r += "<div style=\"margin-top:10px\"><b>&#128167; Pumpenzuweisungen</b>";
    r += "<div id=\"assigns"; r += si; r += "\">";
    // Render existing assignments for this slot
    int assignDisplayIdx = 0;
    for (int ai = 0; ai < sc.assignCount; ai++) {
        if (sc.assignments[ai].slotIndex != (uint8_t)si) continue;
        const SlotPumpAssignment& a = sc.assignments[ai];
        r += "<div class=\"form-row\" style=\"margin-top:6px;align-items:center\" id=\"arow";
        r += si; r += "_"; r += assignDisplayIdx; r += "\">";
        r += "<div class=\"form-col\"><label>Pumpe</label><select name=\"a";
        r += si; r += "_"; r += assignDisplayIdx; r += "_pump\">";
        for (int pi = 0; pi < hw.relayCount; pi++) {
            r += "<option value=\""; r += pi; r += "\"";
            if (a.pumpIndex == (uint8_t)pi) r += " selected";
            r += ">";
            if (hw.pumps[pi].name[0]) r += String(hw.pumps[pi].name);
            else { r += "Pumpe "; r += (pi + 1); }
            r += "</option>";
        }
        r += "</select></div>";
        r += "<div class=\"form-col\"><label>Dauer (s)</label>";
        r += "<input type=\"number\" name=\"a"; r += si; r += "_"; r += assignDisplayIdx;
        r += "_duration\" value=\""; r += a.durationSec; r += "\" min=\"1\" max=\"7200\"></div>";
        r += "<div style=\"padding-top:20px\">";
        r += "<button type=\"button\" onclick=\"deleteAssign("; r += si; r += ","; r += assignDisplayIdx;
        r += ")\" style=\"padding:3px 8px;background:#dc3545;color:#fff;border:none;border-radius:4px;cursor:pointer\">&#10005;</button>";
        r += "</div></div>";
        assignDisplayIdx++;
    }
    r += "</div>";  // assigns{si}
    // hidden field tracking number of assignments for this slot
    r += "<input type=\"hidden\" name=\"s"; r += si; r += "_assignCount\" id=\"aCount";
    r += si; r += "\" value=\""; r += assignDisplayIdx; r += "\">";
    if (hw.relayCount > 0) {
        r += "<button type=\"button\" onclick=\"addAssign("; r += si;
        r += ")\" style=\"margin-top:6px;padding:4px 12px;background:#17a2b8;color:#fff;"
             "border:none;border-radius:4px;cursor:pointer\">+ Pumpe hinzuf&#252;gen</button>";
    } else {
        r += "<p style=\"color:#dc3545;font-size:13px\">&#x26A0; Zuerst Pumpen in der Hardware-Konfiguration anlegen.</p>";
    }
    r += "</div></div>";  // pump-assignments + slot-entry
    return r;
}

static void handleConfigWatering() {
    ConfigManager*  cfg = g_app->getConfigManager();
    HardwareConfig& hw  = cfg->getHardwareConfig();
    SlotConfig&     sc  = cfg->getSlotConfig();

    String page = buildPage(HTML_WATERING_PAGE);

    // Status line
    String wateringStatus;
    if (cfg->isWateringConfigValid()) {
        char buf[100];
        snprintf(buf, sizeof(buf),
                 "<p style='color:#1a6b3c;margin-top:8px'>&#10003; %d Slot(s), %d Zuweisung(en), %d Pumpe(n).</p>",
                 sc.slotCount, sc.assignCount, hw.relayCount);
        wateringStatus = buf;
    } else {
        wateringStatus = "<p style='color:#dc3545;margin-top:8px'>&#10007; Kein g&#252;ltiger Plan: Pumpen konfigurieren und Slots anlegen.</p>";
    }
    page = replaceToken(page, "{watering_status}", wateringStatus);

    // Pump names JSON array for JavaScript add-assign function
    String pumpNamesJson = "[";
    for (int i = 0; i < hw.relayCount; i++) {
        if (i > 0) pumpNamesJson += ",";
        pumpNamesJson += "\"";
        if (hw.pumps[i].name[0]) {
            String n = String(hw.pumps[i].name);
            n.replace("\\", "\\\\");
            n.replace("\"", "\\\"");
            pumpNamesJson += n;
        } else {
            pumpNamesJson += "Pumpe "; pumpNamesJson += (i + 1);
        }
        pumpNamesJson += "\"";
    }
    pumpNamesJson += "]";
    page = replaceToken(page, "{pump_names_json}", pumpNamesJson);
    page = replaceToken(page, "{pumpCount}", String(hw.relayCount));
    page = replaceToken(page, "{slotCount}", String(sc.slotCount));

    // Build slot rows
    String slotRowsHtml;
    for (int i = 0; i < sc.slotCount; i++) {
        slotRowsHtml += buildSlotRowHtml(i, sc.slots[i], sc, hw);
    }
    page = replaceToken(page, "{slot_rows_html}", slotRowsHtml);
    page = replaceToken(page, "{noSlotsMsg}", sc.slotCount == 0 ? "block" : "none");

    g_server->send(200, "text/html; charset=UTF-8", page);
    Serial.println("[Web] GET /config_watering");
}

static void handleSaveWatering() {
    if (g_server->method() != HTTP_POST) {
        g_server->send(405, "text/plain", "Method Not Allowed");
        return;
    }
    ConfigManager*  cfg = g_app->getConfigManager();
    HardwareConfig& hw  = cfg->getHardwareConfig();
    SlotConfig&     sc  = cfg->getSlotConfig();

    int slotCount = constrain(
        g_server->hasArg("slotCount") ? g_server->arg("slotCount").toInt() : 0,
        0, MAX_SLOTS);

    SlotConfig newSc;
    newSc.slotCount  = 0;
    newSc.assignCount = 0;

    for (int si = 0; si < slotCount; si++) {
        if (newSc.slotCount >= MAX_SLOTS) break;

        char key[32];
        snprintf(key, sizeof(key), "s%d_name", si);
        if (!g_server->hasArg(key)) continue;  // slot was deleted

        WateringSlot& s = newSc.slots[newSc.slotCount];
        s = WateringSlot{};
        strlcpy(s.name, g_server->arg(key).c_str(), sizeof(s.name));

        snprintf(key, sizeof(key), "s%d_enabled", si);
        s.enabled = g_server->hasArg(key);

        snprintf(key, sizeof(key), "s%d_trigger", si);
        s.triggerType = (uint8_t)constrain(
            g_server->hasArg(key) ? g_server->arg(key).toInt() : 0, 0, 4);

        snprintf(key, sizeof(key), "s%d_time", si);
        if (g_server->hasArg(key)) {
            String t = g_server->arg(key);
            int c = t.indexOf(':');
            s.fixedHour   = (uint8_t)constrain(c > 0 ? t.substring(0, c).toInt() : 0, 0, 23);
            s.fixedMinute = (uint8_t)constrain(c > 0 ? t.substring(c + 1).toInt() : 0, 0, 59);
        }

        snprintf(key, sizeof(key), "s%d_offsetBase", si);
        s.offsetBase = (uint8_t)constrain(
            g_server->hasArg(key) ? g_server->arg(key).toInt() : 0, 0, 2);

        snprintf(key, sizeof(key), "s%d_offsetMin", si);
        s.offsetMinutes = (int16_t)constrain(
            g_server->hasArg(key) ? g_server->arg(key).toInt() : 0, -720, 720);

        uint8_t days = 0;
        for (int d = 0; d < 7; d++) {
            snprintf(key, sizeof(key), "s%d_d%d", si, d);
            if (g_server->hasArg(key)) days |= (1 << d);
        }
        s.days = days;

        snprintf(key, sizeof(key), "s%d_skipRainMm", si);
        s.skipIfRainMm = g_server->hasArg(key) ? g_server->arg(key).toFloat() : 0.0f;

        snprintf(key, sizeof(key), "s%d_skipRainPct", si);
        s.skipIfRainPct = g_server->hasArg(key) ? g_server->arg(key).toFloat() : 0.0f;

        snprintf(key, sizeof(key), "s%d_aboveTemp", si);
        s.runOnlyAboveTemp = g_server->hasArg(key) ? g_server->arg(key).toFloat() : -99.0f;

        snprintf(key, sizeof(key), "s%d_reduceRainMm", si);
        s.reduceIfRainMm = g_server->hasArg(key) ? g_server->arg(key).toFloat() : 0.0f;

        snprintf(key, sizeof(key), "s%d_reducePct", si);
        s.reducePct = (uint8_t)constrain(
            g_server->hasArg(key) ? g_server->arg(key).toInt() : 50, 1, 99);

        // Pump assignments for this slot
        snprintf(key, sizeof(key), "s%d_assignCount", si);
        int assignCount = g_server->hasArg(key) ? g_server->arg(key).toInt() : 0;

        int curSlotIdx = newSc.slotCount;  // slot index in new config
        for (int ai = 0; ai < assignCount && newSc.assignCount < MAX_SLOT_ASSIGNMENTS; ai++) {
            char akey[32];
            snprintf(akey, sizeof(akey), "a%d_%d_pump", si, ai);
            if (!g_server->hasArg(akey)) continue;

            SlotPumpAssignment& a = newSc.assignments[newSc.assignCount];
            a.slotIndex = (uint8_t)curSlotIdx;
            a.pumpIndex = (uint8_t)constrain(
                g_server->arg(akey).toInt(), 0, hw.relayCount > 0 ? hw.relayCount - 1 : 0);

            snprintf(akey, sizeof(akey), "a%d_%d_duration", si, ai);
            a.durationSec = g_server->hasArg(akey) ?
                constrain(g_server->arg(akey).toInt(), 1, 7200) : 60;
            newSc.assignCount++;
        }
        newSc.slotCount++;
    }

    sc = newSc;
    cfg->saveSlotConfig();
    g_app->requestConfigApply();

    String page = buildPage(HTML_SAVED_LIVE);
    page = replaceToken(page, "{saved_back_url}", "/config_watering");
    g_server->send(200, "text/html; charset=UTF-8", page);
    Serial.printf("[Web] POST /save_watering – %d slot(s), %d assignment(s) saved.\n",
                  sc.slotCount, sc.assignCount);
}

// ─── Weather API endpoint ─────────────────────────────────────────────────────

static void handleApiWeather() {
    // ?refresh=1 forces an immediate weather update before returning
    if (g_server->hasArg("refresh")) {
        WeatherManager* wm = g_app->getWeatherManager();
        if (wm) wm->fetchNow();
    }

    WeatherManager* wm = g_app->getWeatherManager();
    JsonDocument doc;
    if (wm && wm->isAvailable()) {
        const WeatherData& w = wm->getData();
        doc["available"]       = true;
        doc["stale"]           = wm->isStale();
        doc["temperature"]     = w.temperature;
        doc["feelsLike"]       = w.feelsLike;
        doc["humidity"]        = w.humidity;
        doc["precipProb"]      = w.precipProb;
        doc["precipMm"]        = w.precipMm;
        doc["dailyPrecipMm"]   = w.dailyPrecipMm;
        doc["dailyPrecipPct"]  = w.dailyPrecipPct;
        doc["rain"]            = w.rain;
        doc["snow"]            = w.snow;
        doc["windSpeed"]       = w.windSpeed;
        doc["windDir"]         = w.windDir;
        doc["tempMax"]         = w.tempMax;
        doc["tempMin"]         = w.tempMin;
        doc["sunrise"]         = (long)w.sunrise;
        doc["sunset"]          = (long)w.sunset;
        doc["lastUpdate"]      = (long)w.lastUpdate;
    } else {
        doc["available"] = false;
    }
    String json;
    serializeJson(doc, json);
    g_server->send(200, "application/json", json);
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
