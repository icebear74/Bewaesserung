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

// ─── Helper: build per-pump HTML row ─────────────────────────────────────────

static String buildPumpRowHtml(int i, const PumpEntry& p) {
    String r;
    r.reserve(900);
    r += "<div class=\"pump-entry\" style=\"border:1px solid #ddd;padding:10px;margin-bottom:10px;border-radius:4px\">";
    r += "<b>Pumpe "; r += (i + 1); r += "</b>";
    r += "<div class=\"form-row\" style=\"margin-top:6px\">";
    r += "<div class=\"form-col\"><label><input type=\"checkbox\" name=\"p"; r += i; r += "_enabled\"";
    if (p.enabled) r += " checked";
    r += "> Aktiv</label></div>";
    r += "<div class=\"form-col\"><label>Name</label><input type=\"text\" name=\"p"; r += i; r += "_name\" value=\"";
    r += String(p.name); r += "\" maxlength=\"31\"></div></div>";
    // Output type selector
    r += "<div class=\"form-row\">";
    r += "<div class=\"form-col\"><label>Ausgangstyp</label><select name=\"p"; r += i; r += "_type\" onchange=\"toggleOutType("; r += i; r += ",this.value)\">";
    r += "<option value=\"0\""; if (p.outputType == OUTPUT_TYPE_GPIO)    r += " selected"; r += ">GPIO-Pin</option>";
    r += "<option value=\"1\""; if (p.outputType == OUTPUT_TYPE_PCF8574) r += " selected"; r += ">PCF8574 / PCF8575 (I2C)</option>";
    r += "</select></div></div>";
    // GPIO-specific fields
    bool isI2C = (p.outputType == OUTPUT_TYPE_PCF8574);
    r += "<div id=\"gpio"; r += i; r += "\" style=\"display:"; r += (isI2C ? "none" : "block"); r += "\">";
    r += "<div class=\"form-row\">";
    r += "<div class=\"form-col\"><label>GPIO-Pin (-1 = inaktiv)</label><input type=\"number\" name=\"p"; r += i; r += "_pin\" value=\"";
    r += p.pin; r += "\" min=\"-1\" max=\"39\"></div></div></div>";
    // I2C-specific fields
    r += "<div id=\"i2c"; r += i; r += "\" style=\"display:"; r += (isI2C ? "block" : "none"); r += "\">";
    r += "<div class=\"form-row\">";
    r += "<div class=\"form-col\"><label>I2C-Adresse (dezimal, z.B. 32=0x20)</label><input type=\"number\" name=\"p"; r += i; r += "_i2cAddr\" value=\"";
    r += p.i2cAddress; r += "\" min=\"32\" max=\"63\" placeholder=\"32\"></div>";
    r += "<div class=\"form-col\"><label>Kanal / Pin (0-15)</label><input type=\"number\" name=\"p"; r += i; r += "_i2cChan\" value=\"";
    r += p.i2cChannel; r += "\" min=\"0\" max=\"15\"></div></div></div>";
    // Common fields
    r += "<div class=\"form-row\" style=\"margin-top:4px\">";
    r += "<div class=\"form-col\"><label><input type=\"checkbox\" name=\"p"; r += i; r += "_invert\"";
    if (p.invertLogic) r += " checked";
    r += "> Aktiv-LOW (invertiert)</label></div>";
    r += "<div class=\"form-col\"><label>Max. Test-Laufzeit (s)</label><input type=\"number\" name=\"p"; r += i; r += "_maxRuntime\" value=\"";
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
    page = replaceToken(page, "{pumpCount}", String(hw.relayCount));

    String pumpRowsHtml;
    for (int i = 0; i < hw.relayCount; i++) {
        pumpRowsHtml += buildPumpRowHtml(i, hw.pumps[i]);
    }
    page = replaceToken(page, "{pump_rows_html}", pumpRowsHtml);
    g_server->send(200, "text/html; charset=UTF-8", page);
    Serial.println("[Web] GET /config_hardware");
}

static void handleSaveHardware() {
    if (g_server->method() != HTTP_POST) {
        g_server->send(405, "text/plain", "Method Not Allowed");
        return;
    }
    HardwareConfig newHw;
    newHw.relayCount = constrain(
        g_server->hasArg("pumpCount") ? g_server->arg("pumpCount").toInt() : 0,
        0, MAX_RELAY_COUNT);

    for (int i = 0; i < MAX_RELAY_COUNT; i++) {
        PumpEntry& p = newHw.pumps[i];
        // Elements beyond relayCount keep their default-initialized (disabled) state.
        if (i < newHw.relayCount) {
            char key[24];
            snprintf(key, sizeof(key), "p%d_enabled", i);    p.enabled       = g_server->hasArg(key);
            snprintf(key, sizeof(key), "p%d_type", i);       p.outputType    = (uint8_t)constrain(g_server->hasArg(key) ? g_server->arg(key).toInt() : 0, 0, 1);
            snprintf(key, sizeof(key), "p%d_pin", i);        p.pin           = g_server->hasArg(key) ? g_server->arg(key).toInt() : -1;
            snprintf(key, sizeof(key), "p%d_i2cAddr", i);    p.i2cAddress    = (uint8_t)constrain(g_server->hasArg(key) ? g_server->arg(key).toInt() : 0x20, 0x20, 0x3F);
            snprintf(key, sizeof(key), "p%d_i2cChan", i);    p.i2cChannel    = (uint8_t)constrain(g_server->hasArg(key) ? g_server->arg(key).toInt() : 0, 0, 15);
            snprintf(key, sizeof(key), "p%d_invert", i);     p.invertLogic   = g_server->hasArg(key);
            snprintf(key, sizeof(key), "p%d_maxRuntime", i); p.maxRuntimeSec = g_server->hasArg(key) ? constrain(g_server->arg(key).toInt(), 1, 3600) : 300;
            snprintf(key, sizeof(key), "p%d_name", i);
            if (g_server->hasArg(key)) strlcpy(p.name, g_server->arg(key).c_str(), sizeof(p.name));
            snprintf(key, sizeof(key), "p%d_notes", i);
            if (g_server->hasArg(key)) strlcpy(p.notes, g_server->arg(key).c_str(), sizeof(p.notes));
        }
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
    // Validate: check for duplicate PCF8574 address+channel combinations
    for (int i = 0; i < newHw.relayCount; i++) {
        const PumpEntry& pi = newHw.pumps[i];
        if (!pi.enabled || pi.outputType != OUTPUT_TYPE_PCF8574) continue;
        for (int j = i + 1; j < newHw.relayCount; j++) {
            const PumpEntry& pj = newHw.pumps[j];
            if (!pj.enabled || pj.outputType != OUTPUT_TYPE_PCF8574) continue;
            if (pi.i2cAddress == pj.i2cAddress && pi.i2cChannel == pj.i2cChannel) {
                String page = buildPage(HTML_ERROR_PAGE);
                page = replaceToken(page, "{error_msg}",
                    "Doppelte I2C-Belegung: PCF8574 Adresse 0x" +
                    String(pi.i2cAddress, HEX) + " Kanal " + String(pi.i2cChannel) +
                    " ist Pumpe " + String(i + 1) + " und Pumpe " + String(j + 1) + " zugewiesen.");
                page = replaceToken(page, "{back_url}", "/config_hardware");
                g_server->send(400, "text/html; charset=UTF-8", page);
                return;
            }
        }
    }

    g_app->getConfigManager()->getHardwareConfig() = newHw;
    g_app->getConfigManager()->saveHardwareConfig();
    g_app->requestConfigApply();

    String page = buildPage(HTML_SAVED_LIVE);
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

// ─── Helper: build watering entry HTML row ────────────────────────────────────

static String buildWateringEntryHtml(int n, const WateringEntry& e, const HardwareConfig& hw) {
    const char* dayLabels[] = {"Mo","Di","Mi","Do","Fr","Sa","So"};
    String r;
    r.reserve(800);
    r += "<div class=\"pump-entry\" id=\"e"; r += n; r += "\" style=\"border:1px solid #ddd;padding:10px;margin-bottom:8px;border-radius:4px\">";
    r += "<div class=\"form-row\">";
    r += "<div class=\"form-col\"><label>Pumpe</label><select name=\"e"; r += n; r += "_relay\">";
    for (int ri = 0; ri < hw.relayCount; ri++) {
        r += "<option value=\""; r += ri; r += "\"";
        if (e.relay == ri) r += " selected";
        r += ">";
        if (hw.pumps[ri].name[0]) r += String(hw.pumps[ri].name);
        else { r += "Pumpe "; r += (ri + 1); }
        r += "</option>";
    }
    r += "</select></div>";
    char timeBuf[8];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", e.hour, e.minute);
    r += "<div class=\"form-col\"><label>Startzeit</label><input type=\"time\" name=\"e"; r += n; r += "_time\" value=\""; r += timeBuf; r += "\"></div>";
    r += "<div class=\"form-col\"><label>Dauer (s)</label><input type=\"number\" name=\"e"; r += n; r += "_duration\" value=\""; r += e.durationSec; r += "\" min=\"1\" max=\"7200\"></div>";
    r += "</div><div style=\"margin-top:6px\">";
    for (int d = 0; d < 7; d++) {
        r += "<label style=\"margin-right:8px\"><input type=\"checkbox\" name=\"e"; r += n; r += "_d"; r += d; r += "\"";
        if (e.days & (1 << d)) r += " checked";
        r += "> "; r += dayLabels[d]; r += "</label>";
    }
    r += "</div><div style=\"margin-top:6px\">";
    r += "<label><input type=\"checkbox\" name=\"e"; r += n; r += "_active\"";
    if (e.active) r += " checked";
    r += "> Aktiv</label>";
    r += " <button type=\"button\" onclick=\"delEntry("; r += n; r += ")\" ";
    r += "style=\"margin-left:12px;padding:3px 10px;background:#dc3545;color:#fff;border:none;border-radius:4px;cursor:pointer\">&#10005; L&#246;schen</button>";
    r += "</div></div>";
    return r;
}

static void handleConfigWatering() {
    ConfigManager*  cfg = g_app->getConfigManager();
    HardwareConfig& hw  = cfg->getHardwareConfig();
    WateringConfig& wc  = cfg->getWateringConfig();

    String page = buildPage(HTML_WATERING_PAGE);

    // Status line
    String wateringStatus;
    if (cfg->isWateringConfigValid()) {
        char buf[80];
        snprintf(buf, sizeof(buf),
                 "<p style='color:#1a6b3c;margin-top:8px'>&#10003; %d Eintr&#228;ge konfiguriert, %d Pumpen verf&#252;gbar.</p>",
                 wc.count, hw.relayCount);
        wateringStatus = buf;
    } else {
        wateringStatus = "<p style='color:#dc3545;margin-top:8px'>&#10007; Kein g&#252;ltiger Plan: Hardware oder Eintr&#228;ge fehlen.</p>";
    }
    page = replaceToken(page, "{watering_status}", wateringStatus);

    // Relay names JSON array for JavaScript
    String relayNamesJson = "[";
    for (int i = 0; i < hw.relayCount; i++) {
        if (i > 0) relayNamesJson += ",";
        relayNamesJson += "\"";
        if (hw.pumps[i].name[0]) {
            String n = String(hw.pumps[i].name);
            n.replace("\"", "\\\"");
            relayNamesJson += n;
        } else {
            relayNamesJson += "Pumpe "; relayNamesJson += (i + 1);
        }
        relayNamesJson += "\"";
    }
    relayNamesJson += "]";
    page = replaceToken(page, "{relay_names_json}", relayNamesJson);
    page = replaceToken(page, "{relayCount}", String(hw.relayCount));
    page = replaceToken(page, "{nextIdx}", String(wc.count));

    // Build entry rows
    String entryRowsHtml;
    for (int i = 0; i < wc.count; i++) {
        entryRowsHtml += buildWateringEntryHtml(i, wc.entries[i], hw);
    }
    page = replaceToken(page, "{entry_rows_html}", entryRowsHtml);

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
    WateringConfig& wc  = cfg->getWateringConfig();
    wc.count = 0;

    // Scan indices up to SCAN_LIMIT to accommodate gaps left by deleted entries:
    // JS nextIdx only ever increments, so deleted+re-added cycles can raise
    // the highest index beyond MAX_WATERING_ENTRIES.  4x gives plenty of headroom.
    constexpr int ENTRY_INDEX_SCAN_LIMIT = MAX_WATERING_ENTRIES * 4;
    for (int n = 0; n < ENTRY_INDEX_SCAN_LIMIT && wc.count < MAX_WATERING_ENTRIES; n++) {
        char key[20];
        snprintf(key, sizeof(key), "e%d_relay", n);
        if (!g_server->hasArg(key)) continue;

        WateringEntry& e = wc.entries[wc.count++];
        e.relay = constrain(g_server->arg(key).toInt(), 0, hw.relayCount > 0 ? hw.relayCount - 1 : 0);

        // Parse time field (HH:MM)
        snprintf(key, sizeof(key), "e%d_time", n);
        String timeStr = g_server->hasArg(key) ? g_server->arg(key) : "00:00";
        int colon = timeStr.indexOf(':');
        e.hour   = constrain(colon > 0 ? timeStr.substring(0, colon).toInt() : 0, 0, 23);
        e.minute = constrain(colon > 0 ? timeStr.substring(colon + 1).toInt() : 0, 0, 59);

        snprintf(key, sizeof(key), "e%d_duration", n);
        e.durationSec = g_server->hasArg(key) ? constrain(g_server->arg(key).toInt(), 1, 7200) : 60;

        snprintf(key, sizeof(key), "e%d_active", n);
        e.active = g_server->hasArg(key);

        uint8_t days = 0;
        for (int d = 0; d < 7; d++) {
            snprintf(key, sizeof(key), "e%d_d%d", n, d);
            if (g_server->hasArg(key)) days |= (1 << d);
        }
        e.days = days;
    }

    cfg->saveWateringConfig();
    String page = buildPage(HTML_SAVED_LIVE);
    page = replaceToken(page, "{saved_back_url}", "/config_watering");
    g_server->send(200, "text/html; charset=UTF-8", page);
    Serial.printf("[Web] POST /save_watering – %d entries saved.\n", wc.count);
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
