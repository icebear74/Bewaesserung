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
#include "WateringDecisionEngine.h"
#include <WebServer.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>

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

static String formatKiB(size_t bytes) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f KiB", bytes / 1024.0f);
    return String(buf);
}

static int daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int)doe - 719468;
}

static const int MAX_NEXT_SEARCH_DAYS = 90;

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
static void handleWateringTest();
static void handleApiWeather();
static void handleApiWateringSimulate();
static void handleApiWateringStatus();
static void handleNotFound();
static String formatDateTimeLocal(time_t ts);
static String getSlotLabel(const WateringSlot& slot, int idx);
static String getPumpLabel(const HardwareConfig& hw, int idx);
static const char* actionToText(WateringDecisionAction action);

struct NextSlotDecisionInfo {
    bool found = false;
    time_t triggerTime = 0;
    WateringDecisionResult result;
};

static bool findPlanForPump(const WateringDecisionResult& res, int pumpIndex, WateringDecisionPumpPlan& outPlan);
static bool findNextSlotDecision(int slotIndex,
                                 time_t nowLocal,
                                 const SlotConfig& sc,
                                 const HardwareConfig& hw,
                                 const WeatherData* weatherData,
                                 bool weatherAvailable,
                                 bool weatherStale,
                                 NextSlotDecisionInfo& out);

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
    g_server->on("/watering_test",   HTTP_GET,  handleWateringTest);
    g_server->on("/api/weather",     HTTP_GET,  handleApiWeather);
    g_server->on("/api/watering_simulate", HTTP_POST, handleApiWateringSimulate);
    g_server->on("/api/watering_status", HTTP_GET, handleApiWateringStatus);
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

    // ── Pump status + next decisions (shared decision engine) ─────────────────
    HardwareConfig& hw = cfg->getHardwareConfig();
    SlotConfig& sc = cfg->getSlotConfig();
    String pumpHtml;
    RelayManager* rm = g_app->getRelayManager();
    const WeatherData* weatherData = (wm && wm->isAvailable()) ? &wm->getData() : nullptr;
    bool weatherAvailable = (wm && wm->isAvailable());
    bool weatherStale = (wm && wm->isStale());
    time_t now = time(nullptr);
    if (hw.relayCount > 0) {
        pumpHtml += "<h2 style='margin-top:4px;color:#1a6b3c'>💧 Pumpen (Live-Entscheidung)</h2>";
        pumpHtml += "<div class='table-wrap'><table class='compact-table'><tr><th>Pumpe</th><th>Status</th><th>Nächster Lauf</th><th>Entscheidung</th><th>Grund</th><th>Letzter Start/Stop</th></tr>";
        for (int i = 0; i < hw.relayCount; i++) {
            RelayManager::PumpRuntimeInfo rt;
            bool haveRt = rm && rm->getPumpRuntimeInfo(i, rt);
            bool active = haveRt ? rt.running : (rm && rm->isActive(i));
            String status = active ? "<span style='color:#dc3545;font-weight:bold'>EIN 🔴</span>"
                                   : "<span style='color:#1a6b3c'>AUS</span>";
            if (!hw.pumps[i].enabled) status += " <span style='color:#777'>(deaktiviert)</span>";

            bool foundNext = false;
            time_t bestTs = 0;
            String bestSlot = "–";
            WateringDecisionPumpPlan bestPlan;
            String bestWarnings = "";
            for (int si = 0; si < sc.slotCount; si++) {
                NextSlotDecisionInfo next;
                if (!findNextSlotDecision(si, now, sc, hw, weatherData, weatherAvailable, weatherStale, next)) continue;
                WateringDecisionPumpPlan pp;
                if (!findPlanForPump(next.result, i, pp)) continue;
                if (!foundNext || next.triggerTime < bestTs) {
                    foundNext = true;
                    bestTs = next.triggerTime;
                    bestSlot = getSlotLabel(sc.slots[si], si);
                    bestPlan = pp;
                    bestWarnings = next.result.warnings;
                }
            }

            String lastRun = "–";
            if (haveRt && (rt.lastStartEpoch > 0 || rt.lastStopEpoch > 0)) {
                lastRun = formatDateTimeLocal(rt.lastStartEpoch) + " / " + formatDateTimeLocal(rt.lastStopEpoch);
            }
            String nextCell = foundNext ? (bestSlot + "<br><span style='color:#555'>" + formatDateTimeLocal(bestTs) + "</span>") : "–";
            String action = foundNext ? String(actionToLabelDe(bestPlan.action)) : "aussetzen";
            String reason = foundNext ? String(bestPlan.reason) : "Keine Zuweisung";
            if (bestWarnings.length()) reason += "<br><span style='color:#b26a00'>⚠ " + bestWarnings + "</span>";
            if (foundNext) {
                reason += "<br><span style='color:#666'>Policy: " + String(bestPlan.policySource) + "</span>";
                if (strlen(bestPlan.appliedRules) > 0) {
                    reason += "<br><span style='color:#555'>Regeln: " + String(bestPlan.appliedRules) + "</span>";
                }
            }
            pumpHtml += "<tr><td>" + getPumpLabel(hw, i) + "</td><td>" + status + "</td><td>" + nextCell +
                        "</td><td>" + action + (foundNext ? (" (" + String(bestPlan.durationSec) + "s)") : "") +
                        "</td><td>" + reason + "</td><td>" + lastRun + "</td></tr>";
        }
        pumpHtml += "</table></div>";
    } else {
        pumpHtml = "<p style='color:#999;font-style:italic'>Keine Pumpen konfiguriert.</p>";
    }
    page = replaceToken(page, "{pump_status_html}", pumpHtml);

    // ── Weather section ───────────────────────────────────────────────────────
    String weatherHtml;
    {
        size_t heapFree  = ESP.getFreeHeap();
        size_t heapMin   = ESP.getMinFreeHeap();
        size_t heapTotal = ESP.getHeapSize();
        size_t psramFree = ESP.getFreePsram();
        size_t psramTotal = ESP.getPsramSize();

        weatherHtml += "<div style='margin-top:12px;padding:10px;border:1px solid #dde7dd;border-radius:6px;background:#f7fbff'>";
        weatherHtml += "<h2 style='margin:0 0 8px 0;color:#1a4f8f'>🧠 Speicher</h2><table>";
        weatherHtml += "<tr><td>Heap frei</td><td>" + formatKiB(heapFree) + "</td></tr>";
        weatherHtml += "<tr><td>Heap Minimum</td><td>" + formatKiB(heapMin) + "</td></tr>";
        weatherHtml += "<tr><td>Heap gesamt</td><td>" + formatKiB(heapTotal) + "</td></tr>";
        if (psramTotal > 0) {
            weatherHtml += "<tr><td>PSRAM frei</td><td>" + formatKiB(psramFree) + "</td></tr>";
            weatherHtml += "<tr><td>PSRAM gesamt</td><td>" + formatKiB(psramTotal) + "</td></tr>";
        } else {
            weatherHtml += "<tr><td>PSRAM</td><td>nicht verfügbar</td></tr>";
        }
        weatherHtml += "</table></div>";
    }
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
        if (wm->getLastHttpCode() != 0) {
            weatherHtml += "<tr><td>Letzter HTTP-Status</td><td>" + String(wm->getLastHttpCode()) + "</td></tr>";
        }
        if (strlen(wm->getLastError()) > 0) {
            weatherHtml += "<tr><td>Letzter Fehler</td><td><code>" + String(wm->getLastError()) + "</code></td></tr>";
        }
        if (strlen(wm->getLastRequestUrl()) > 0) {
            weatherHtml += "<tr><td>Letzte URL</td><td><code style='word-break:break-all'>" + String(wm->getLastRequestUrl()) + "</code></td></tr>";
        }
        weatherHtml += "</table></div>";
        weatherHtml += "</div></div>";  // flex + outer div
    } else if (wm) {
        weatherHtml += "<div style='margin-top:16px;border-top:1px solid #eee;padding-top:12px'>";
        weatherHtml += "<p style='color:#999'>🌤️ Keine Wetterdaten verfügbar (Internetverbindung erforderlich).</p>";
        if (strlen(wm->getLastError()) > 0) {
            weatherHtml += "<p><b>Letzter Fehler:</b> <code>" + String(wm->getLastError()) + "</code></p>";
        }
        if (strlen(wm->getLastRequestUrl()) > 0) {
            weatherHtml += "<p><b>Letzte URL:</b><br><code style='word-break:break-all'>" + String(wm->getLastRequestUrl()) + "</code></p>";
        }
        weatherHtml += "<button class='btn' style='margin-top:8px;padding:6px 14px;font-size:13px' ";
        weatherHtml += "onclick=\"fetch('/api/weather?refresh=1').then(()=>location.reload())\">🔄 Wetter aktualisieren</button>";
        weatherHtml += "</div>";
    }
    // ── Global next slot summary ───────────────────────────────────────────────
    {
        time_t now = time(nullptr);
        time_t nextTs = 0;
        String nextName = "–";
        String nextReason = "Kein nächster Slot gefunden.";
        bool fallbackActive = false;
        for (int si = 0; si < sc.slotCount; si++) {
            NextSlotDecisionInfo next;
            if (!findNextSlotDecision(si, now, sc, hw, weatherData, weatherAvailable, weatherStale, next)) continue;
            if (nextTs == 0 || next.triggerTime < nextTs) {
                nextTs = next.triggerTime;
                nextName = getSlotLabel(sc.slots[si], si);
                nextReason = next.result.reason;
                fallbackActive = next.result.usedFallbackTime;
            }
        }
        weatherHtml = "<div style='margin-top:12px;padding:10px;border:1px solid #dde7dd;border-radius:6px;background:#f8fff8'>"
                      "<b>Nächster Slot:</b> " + nextName + " @ " + formatDateTimeLocal(nextTs) +
                      "<br><b>Entscheidung:</b> " + nextReason +
                      "<br><b>Fallback aktiv:</b> " + String(fallbackActive ? "ja" : "nein") +
                      "<br><b>Wetter frisch:</b> " + String((wm && wm->isAvailable() && !wm->isStale()) ? "ja" : "nein") +
                      "</div>" + weatherHtml;
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
    bool firstOtaSetup = (cfg.otaPassword[0] == '\0');
    String otaPwField;
    if (firstOtaSetup) {
        otaPwField = "<input type=\"text\" id=\"otaPassword\" name=\"otaPassword\" "
                     "placeholder=\"OTA-Passwort festlegen\" maxlength=\"63\">";
    } else {
        otaPwField = "<input type=\"password\" id=\"otaPassword\" name=\"otaPassword\" "
                     "placeholder=\"Leer lassen = keine Änderung\" maxlength=\"63\">";
    }
    page = replaceToken(page, "{ota_password_field}", otaPwField);
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
    if (g_server->hasArg("otaPassword") && !g_server->arg("otaPassword").isEmpty()) {
        strlcpy(cfg.otaPassword, g_server->arg("otaPassword").c_str(), sizeof(cfg.otaPassword));
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
    float oldLatitude = cfg.latitude;
    float oldLongitude = cfg.longitude;
    if (g_server->hasArg("latitude"))     cfg.latitude  = g_server->arg("latitude").toFloat();
    if (g_server->hasArg("longitude"))    cfg.longitude = g_server->arg("longitude").toFloat();
    if (g_server->hasArg("locationName")) strlcpy(cfg.locationName, g_server->arg("locationName").c_str(), sizeof(cfg.locationName));
    g_app->getConfigManager()->saveDeviceConfig();

    // Ignore tiny float round-trip differences from the form; ~5 m is still the
    // same practical location for weather forecast purposes.
    static const float LOCATION_EPSILON = 0.00005f;
    bool locationChanged = (fabsf(cfg.latitude - oldLatitude) > LOCATION_EPSILON) ||
                           (fabsf(cfg.longitude - oldLongitude) > LOCATION_EPSILON);
    WeatherManager* wm = g_app->getWeatherManager();
    if (locationChanged && wm) {
        wm->requestRefresh();
        wm->fetchNow();
    }

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
    // Static constant explaining the scan factor: since the JS counter _nextPumpIdx is
    // never decremented after deletions, the form may submit a pumpCount much larger
    // than MAX_RELAY_COUNT.  Scanning up to PUMP_SCAN_FACTOR × MAX_RELAY_COUNT allows
    // for that many add/delete cycles within a single editing session without capping.
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

static String epochDayToDateString(uint16_t epochDay) {
    if (epochDay == 0) return "";
    time_t t = (time_t)epochDay * 86400;
    struct tm lt;
    gmtime_r(&t, &lt);
    char buf[11];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
    return String(buf);
}

static String describeRepeatRule(const WateringSlot& slot) {
    if (slot.repeatMode == REPEAT_INTERVAL_DAYS) {
        String a = epochDayToDateString(slot.intervalAnchorDay);
        if (a.isEmpty()) a = "heute";
        return "Intervall: alle " + String(slot.intervalDays) + " Tag(e), Start " + a;
    }
    const char* dayLabels[] = {"Mo","Di","Mi","Do","Fr","Sa","So"};
    String r = "Wochentage: ";
    bool first = true;
    for (int d = 0; d < 7; d++) {
        if (!(slot.days & (1 << d))) continue;
        if (!first) r += ", ";
        r += dayLabels[d];
        first = false;
    }
    if (first) r += "keine";
    return r;
}

// ─── Helper: build watering slot HTML row (server-side) ──────────────────────

static String buildSlotRowHtml(int si, const WateringSlot& slot,
                                const SlotConfig& sc, const HardwareConfig& hw) {
    (void)sc;
    (void)hw;
    const char* dayLabels[] = {"Mo","Di","Mi","Do","Fr","Sa","So"};
    const char* trigLabels[] = {"Feste Uhrzeit","Sonnenaufgang","Sonnenuntergang",
                                 "Mittagszeit","Offset (relativ zu Referenz)"};
    const char* baseLabels[] = {"Sonnenaufgang","Sonnenuntergang","Mittagszeit"};
    String r;
    r.reserve(1700);
    r += "<div class=\"pump-entry\" id=\"slot"; r += si;
    r += "\" style=\"border:1px solid #b3d4b3;padding:12px;margin-bottom:12px;border-radius:6px;background:#f9fff9\">";
    // Header
    r += "<div style=\"display:flex;justify-content:space-between;align-items:center;margin-bottom:8px\">";
    r += "<b style=\"font-size:1.05em\">&#128337; Slot ";  r += (si + 1);
    if (slot.name[0]) { r += " &ndash; "; r += String(slot.name); }
    r += "</b>";
    r += "<div style=\"display:flex;gap:6px;flex-wrap:wrap\">";
    r += "<button type=\"button\" onclick=\"editSlot("; r += si;
    r += ")\" style=\"padding:3px 10px;background:#17a2b8;color:#fff;border:none;border-radius:4px;cursor:pointer\">&#9998; Bearbeiten</button>";
    r += "<button type=\"button\" onclick=\"deleteSlot("; r += si;
    r += ")\" style=\"padding:3px 10px;background:#dc3545;color:#fff;border:none;border-radius:4px;cursor:pointer\">&#10005; L&#246;schen</button>";
    r += "</div>";
    r += "</div>";
    char summaryBuf[220];
    snprintf(summaryBuf, sizeof(summaryBuf), "Auslöser: %s | Zeit/Fallback: %02u:%02u",
             trigLabels[slot.triggerType], slot.fixedHour, slot.fixedMinute);
    r += "<div style=\"font-size:12px;color:#456;margin-bottom:8px\">";
    r += String(summaryBuf);
    r += " | ";
    r += describeRepeatRule(slot);
    r += "</div>";
    r += "<div id=\"slotBody"; r += si; r += "\" style=\"display:none\">";
    // Enabled + Name row
    r += "<div class=\"form-row\">";
    r += "<div class=\"form-col\"><label title=\"Nur aktive Slots werden geprüft.\"><input type=\"checkbox\" name=\"s"; r += si; r += "_enabled\"";
    if (slot.enabled) r += " checked";
    r += "> Aktiv</label></div>";
    r += "<div class=\"form-col\"><label title=\"Ein Slot ist ein reiner Zeit-Auslöser. Wetterlogik gehört nicht hier hinein.\">Name</label><input type=\"text\" name=\"s"; r += si;
    r += "_name\" value=\""; r += String(slot.name); r += "\" maxlength=\"31\" required></div>";
    r += "</div>";
    // Trigger type + fixed time
    r += "<div class=\"form-row\">";
    r += "<div class=\"form-col\"><label title=\"Legt fest, worauf sich der Slot zeitlich bezieht: feste Uhrzeit, Sonnenaufgang, Sonnenuntergang oder Offset relativ dazu.\">Ausl&ouml;ser</label><select name=\"s"; r += si;
    r += "_trigger\" onchange=\"onTriggerChange("; r += si; r += ",this.value)\">";
    for (int t = 0; t < 5; t++) {
        r += "<option value=\""; r += t; r += "\"";
        if (slot.triggerType == (uint8_t)t) r += " selected";
        r += ">"; r += trigLabels[t]; r += "</option>";
    }
    r += "</select></div>";
    char timeBuf[8];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", slot.fixedHour, slot.fixedMinute);
    r += "<div class=\"form-col\"><label title=\"Bei astronomischen Triggern wird diese Uhrzeit verwendet, falls keine Wetter-/Astronomiedaten verfügbar sind.\">Uhrzeit / Fallback</label>";
    r += "<input type=\"time\" name=\"s"; r += si; r += "_time\" value=\""; r += timeBuf; r += "\"></div>";
    r += "</div>";
    // Offset fields (visible only when trigger=4)
    bool isOffset = (slot.triggerType == TRIGGER_OFFSET);
    r += "<div id=\"offsetRow"; r += si;
    r += "\" style=\"display:"; r += (isOffset ? "flex" : "none"); r += ";\" class=\"form-row\">";
    r += "<div class=\"form-col\"><label title=\"Relativ bedeutet: Der Start wird von Sonnenaufgang, Sonnenuntergang oder Mittagszeit aus berechnet.\">Offset-Basis</label><select name=\"s"; r += si; r += "_offsetBase\">";
    for (int b = 0; b < 3; b++) {
        r += "<option value=\""; r += b; r += "\"";
        if (slot.offsetBase == (uint8_t)b) r += " selected";
        r += ">"; r += baseLabels[b]; r += "</option>";
    }
    r += "</select></div>";
    r += "<div class=\"form-col\"><label title=\"Negativ = davor, positiv = danach. Beispiel: -30 bedeutet 30 Minuten vor der gewählten Basis.\">Offset (Min., negativ = davor, positiv = danach)</label>";
    r += "<input type=\"number\" name=\"s"; r += si; r += "_offsetMin\" value=\"";
    r += slot.offsetMinutes; r += "\" min=\"-720\" max=\"720\"></div>";
    r += "</div>";
    r += "<div class=\"form-row\">";
    r += "<div class=\"form-col\"><label title=\"Wochentage = feste Tage. Intervall = alle N Tage ab dem Ankerdatum.\">Wiederholung</label><select name=\"s"; r += si;
    r += "_repeatMode\" onchange=\"onRepeatModeChange("; r += si; r += ",this.value)\">";
    r += "<option value=\"0\"";
    if (slot.repeatMode == REPEAT_WEEKDAYS) r += " selected";
    r += ">Wochentage</option>";
    r += "<option value=\"1\"";
    if (slot.repeatMode == REPEAT_INTERVAL_DAYS) r += " selected";
    r += ">Intervall (alle N Tage)</option></select></div>";
    r += "</div>";
    bool intervalMode = (slot.repeatMode == REPEAT_INTERVAL_DAYS);
    r += "<div id=\"daysRow"; r += si; r += "\" style=\"display:"; r += (intervalMode ? "none" : "block"); r += ";margin-top:6px\">";
    for (int d = 0; d < 7; d++) {
        r += "<label style=\"margin-right:7px\"><input type=\"checkbox\" name=\"s"; r += si;
        r += "_d"; r += d; r += "\"";
        if (slot.days & (1 << d)) r += " checked";
        r += "> "; r += dayLabels[d]; r += "</label>";
    }
    r += "</div>";
    r += "<div id=\"intervalRow"; r += si;
    r += "\" style=\"display:"; r += (intervalMode ? "flex" : "none");
    r += "\" class=\"form-row\">";
    r += "<div class=\"form-col\"><label title=\"Beispiel: 3 bedeutet alle drei Tage.\">Alle N Tage</label><input type=\"number\" name=\"s"; r += si;
    r += "_intervalDays\" value=\""; r += slot.intervalDays; r += "\" min=\"1\" max=\"90\"></div>";
    r += "<div class=\"form-col\"><label title=\"Ab diesem Datum wird das Intervall gezählt.\">Startdatum (Anker)</label><input type=\"date\" name=\"s"; r += si;
    r += "_intervalAnchor\" value=\""; r += epochDayToDateString(slot.intervalAnchorDay); r += "\"></div></div>";
    r += "</div></div>";  // slot-entry + slotBody
    return r;
}

static String getSlotLabel(const WateringSlot& slot, int idx) {
    String name = slot.name[0] ? String(slot.name) : ("Slot " + String(idx + 1));
    return String(idx + 1) + " - " + name;
}

static String getPumpLabel(const HardwareConfig& hw, int idx) {
    if (idx < 0 || idx >= hw.relayCount) return "Ungültige Pumpe";
    return hw.pumps[idx].name[0] ? String(hw.pumps[idx].name) : ("Pumpe " + String(idx + 1));
}

static String getWeatherTemplateLabel(const SlotConfig& sc, int idx) {
    if (idx < 0 || idx >= sc.weatherTemplateCount) return "Kein Template";
    return sc.weatherTemplates[idx].name[0]
        ? String(sc.weatherTemplates[idx].name)
        : ("Wetter " + String(idx + 1));
}

static const char* weatherRuleActionLabel(uint8_t actionType) {
    switch (actionType) {
        case WEATHER_RULE_REDUCE_RUNTIME: return "Laufzeit verkürzen";
        case WEATHER_RULE_INCREASE_RUNTIME: return "Laufzeit verlängern";
        default: return "Aussetzen";
    }
}

static const char* weatherRuleMetricLabel(uint8_t metric) {
    switch (metric) {
        case WEATHER_METRIC_CURRENT_TEMP: return "Aktuelle Temperatur";
        case WEATHER_METRIC_FORECAST_TEMP_MAX: return "Max. Temperatur in Zeitfenster";
        case WEATHER_METRIC_CURRENT_RAIN_MM: return "Aktueller Niederschlag (mm)";
        case WEATHER_METRIC_CURRENT_RAIN_PROB: return "Aktuelle Regenwahrscheinlichkeit (%)";
        case WEATHER_METRIC_DAILY_RAIN_MM: return "Regen heute (mm)";
        case WEATHER_METRIC_DAILY_RAIN_PROB: return "Regenwahrscheinlichkeit heute (%)";
        case WEATHER_METRIC_FORECAST_RAIN_SUM: return "Regenmenge im Zeitfenster (mm)";
        case WEATHER_METRIC_FORECAST_RAIN_PROB_MAX: return "Max. Regenwahrscheinlichkeit im Zeitfenster (%)";
        default: return "Wetterwert";
    }
}

static bool weatherMetricUsesWindow(uint8_t metric) {
    return metric == WEATHER_METRIC_FORECAST_TEMP_MAX ||
           metric == WEATHER_METRIC_FORECAST_RAIN_SUM ||
           metric == WEATHER_METRIC_FORECAST_RAIN_PROB_MAX;
}

static String buildWeatherRuleSummary(const WeatherRule& rule) {
    String metric = weatherRuleMetricLabel(rule.metric);
    String op = ">=";
    switch (rule.comparison) {
        case WEATHER_OP_GT: op = ">"; break;
        case WEATHER_OP_GTE: op = ">="; break;
        case WEATHER_OP_LT: op = "<"; break;
        case WEATHER_OP_LTE: op = "<="; break;
    }
    String threshold = String(rule.threshold, (rule.metric == WEATHER_METRIC_CURRENT_TEMP || rule.metric == WEATHER_METRIC_FORECAST_TEMP_MAX ||
                                              rule.metric == WEATHER_METRIC_CURRENT_RAIN_MM || rule.metric == WEATHER_METRIC_DAILY_RAIN_MM ||
                                              rule.metric == WEATHER_METRIC_FORECAST_RAIN_SUM) ? 1 : 0);
    String unit = (rule.metric == WEATHER_METRIC_CURRENT_TEMP || rule.metric == WEATHER_METRIC_FORECAST_TEMP_MAX)
                      ? "°C"
                      : ((rule.metric == WEATHER_METRIC_CURRENT_RAIN_PROB || rule.metric == WEATHER_METRIC_DAILY_RAIN_PROB ||
                          rule.metric == WEATHER_METRIC_FORECAST_RAIN_PROB_MAX)
                             ? "%"
                             : " mm");
    String tail = weatherMetricUsesWindow(rule.metric)
                      ? (" in den nächsten " + String(rule.windowHours) + "h")
                      : "";
    if (rule.actionType == WEATHER_RULE_SKIP) {
        return metric + tail + " " + op + " " + threshold + unit + " → Aussetzen";
    }
    String sign = (rule.actionType == WEATHER_RULE_REDUCE_RUNTIME) ? "-" : "+";
    return metric + tail + " " + op + " " + threshold + unit + " → " +
           sign + String(rule.effectPercent) + "% Laufzeit";
}

static String buildWeatherRuleRowHtml(int wi, int ri, const WeatherRule& rule) {
    String html;
    html.reserve(1800);
    bool showWindow = weatherMetricUsesWindow(rule.metric);
    bool showEffect = rule.actionType != WEATHER_RULE_SKIP;
    html += "<div id=\"wtr";
    html += wi;
    html += "_";
    html += ri;
    html += "\" style=\"border:1px solid #dce8f8;border-radius:6px;padding:10px;margin:8px 0;background:#fff\">";
    html += "<div style=\"display:flex;justify-content:space-between;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:8px\"><b>Regel ";
    html += (ri + 1);
    html += "</b><button type=\"button\" onclick=\"deleteWeatherRule(";
    html += wi;
    html += ",";
    html += ri;
    html += ")\" style=\"padding:3px 8px;background:#dc3545;color:#fff;border:none;border-radius:4px;cursor:pointer\">&#10005; Entfernen</button></div>";
    html += "<div class=\"hint-text\" id=\"wt";
    html += wi;
    html += "r";
    html += ri;
    html += "Summary\" style=\"margin-bottom:8px\">";
    html += buildWeatherRuleSummary(rule);
    html += "</div>";
    html += "<div class=\"form-row\">";
    html += "<div class=\"form-col\"><label title=\"Aktive Regeln werden ausgewertet, deaktivierte Regeln bleiben gespeichert.\"><input type=\"checkbox\" name=\"wt";
    html += wi;
    html += "_r";
    html += ri;
    html += "_enabled\"";
    if (rule.enabled) html += " checked";
    html += " onchange=\"onRuleChanged(";
    html += wi;
    html += ",";
    html += ri;
    html += ")\"> Aktiv</label></div>";
    html += "<div class=\"form-col\"><label title=\"Aussetzen stoppt die Pumpe komplett. Verkürzen/Verlängern ändern die Basislaufzeit prozentual.\">Regeltyp</label><select name=\"wt";
    html += wi;
    html += "_r";
    html += ri;
    html += "_action\" onchange=\"onRuleActionChange(";
    html += wi;
    html += ",";
    html += ri;
    html += ",this.value)\">";
    for (int a = WEATHER_RULE_SKIP; a <= WEATHER_RULE_INCREASE_RUNTIME; a++) {
        html += "<option value=\"";
        html += a;
        html += "\"";
        if (rule.actionType == (uint8_t)a) html += " selected";
        html += ">";
        html += weatherRuleActionLabel((uint8_t)a);
        html += "</option>";
    }
    html += "</select></div>";
    html += "</div>";
    html += "<div class=\"form-row\">";
    html += "<div class=\"form-col\"><label title=\"Der Wetterwert wird mit dem Schwellwert verglichen. F\u00fcr Zeitfenster-Regeln werden die n\u00e4chsten Stunden verwendet.\">Wetterwert</label><select name=\"wt";
    html += wi;
    html += "_r";
    html += ri;
    html += "_metric\" onchange=\"onRuleMetricChange(";
    html += wi;
    html += ",";
    html += ri;
    html += ",this.value)\">";
    for (int m = WEATHER_METRIC_CURRENT_TEMP; m <= WEATHER_METRIC_FORECAST_RAIN_PROB_MAX; m++) {
        html += "<option value=\"";
        html += m;
        html += "\"";
        if (rule.metric == (uint8_t)m) html += " selected";
        html += ">";
        html += weatherRuleMetricLabel((uint8_t)m);
        html += "</option>";
    }
    html += "</select></div>";
    html += "<div class=\"form-col\"><label title=\"Vergleicht Wetterwert und Schwellwert. Beispiel: > 30°C.\">Vergleich</label><select name=\"wt";
    html += wi;
    html += "_r";
    html += ri;
    html += "_operator\" onchange=\"onRuleChanged(";
    html += wi;
    html += ",";
    html += ri;
    html += ")\">";
    for (int op = WEATHER_OP_GT; op <= WEATHER_OP_LTE; op++) {
        const char* opText = (op == WEATHER_OP_GT) ? ">" : (op == WEATHER_OP_GTE) ? ">=" : (op == WEATHER_OP_LT) ? "<" : "<=";
        html += "<option value=\"";
        html += op;
        html += "\"";
        if (rule.comparison == (uint8_t)op) html += " selected";
        html += ">";
        html += opText;
        html += "</option>";
    }
    html += "</select></div>";
    html += "<div class=\"form-col\"><label title=\"Ab diesem Wert greift die Regel.\">Schwellwert</label><input type=\"number\" name=\"wt";
    html += wi;
    html += "_r";
    html += ri;
    html += "_threshold\" value=\"";
    html += String(rule.threshold, 1);
    html += "\" step=\"0.1\" oninput=\"onRuleChanged(";
    html += wi;
    html += ",";
    html += ri;
    html += ")\"></div></div>";
    html += "<div id=\"wt";
    html += wi;
    html += "r";
    html += ri;
    html += "WindowRow\" class=\"form-row\" style=\"display:";
    html += (showWindow ? "flex" : "none");
    html += "\"><div class=\"form-col\"><label title=\"Nur f\u00fcr Zeitfenster-Regeln: wie viele n\u00e4chste Stunden ausgewertet werden.\">Zeitfenster (h)</label><input type=\"number\" name=\"wt";
    html += wi;
    html += "_r";
    html += ri;
    html += "_windowHours\" value=\"";
    html += rule.windowHours;
    html += "\" min=\"1\" max=\"48\" oninput=\"onRuleChanged(";
    html += wi;
    html += ",";
    html += ri;
    html += ")\"></div></div>";
    html += "<div id=\"wt";
    html += wi;
    html += "r";
    html += ri;
    html += "EffectRow\" class=\"form-row\" style=\"display:";
    html += (showEffect ? "flex" : "none");
    html += "\"><div class=\"form-col\"><label title=\"Positive Prozentwerte beziehen sich immer auf die Basislaufzeit der Zuweisung.\">Effekt (%)</label><input type=\"number\" name=\"wt";
    html += wi;
    html += "_r";
    html += ri;
    html += "_effectPct\" value=\"";
    html += rule.effectPercent;
    html += "\" min=\"1\" max=\"200\" oninput=\"onRuleChanged(";
    html += wi;
    html += ",";
    html += ri;
    html += ")\"></div></div></div>";
    return html;
}

static String buildWeatherTemplateRowHtml(int wi, const WeatherTemplate& wt) {
    String label = wt.name[0] ? String(wt.name) : ("Wetter " + String(wi + 1));
    String html;
    html.reserve(3200);
    html += "<div class=\"pump-entry\" id=\"wt";
    html += wi;
    html += "\" data-next-rule=\"";
    html += wt.ruleCount;
    html += "\" style=\"border:1px solid #cfe0f6;padding:12px;margin-bottom:12px;border-radius:6px;background:#f7fbff\">";
    html += "<div style=\"display:flex;justify-content:space-between;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:10px\">";
    html += "<b style=\"font-size:1.05em\">🌦️ ";
    html += label;
    html += "</b>";
    html += "<div style=\"display:flex;gap:6px;flex-wrap:wrap\">";
    html += "<button type=\"button\" onclick=\"editWeatherTemplate(";
    html += wi;
    html += ")\" style=\"padding:3px 10px;background:#17a2b8;color:#fff;border:none;border-radius:4px;cursor:pointer\">&#9998; Bearbeiten</button>";
    html += "<button type=\"button\" onclick=\"deleteWeatherTemplate(";
    html += wi;
    html += ")\" style=\"padding:3px 10px;background:#dc3545;color:#fff;border:none;border-radius:4px;cursor:pointer\">&#10005; L&#246;schen</button>";
    html += "</div></div>";
    html += "<div class=\"form-row\"><div class=\"form-col\"><label>Name</label><input type=\"text\" name=\"wt";
    html += wi;
    html += "_name\" value=\"";
    html += String(wt.name);
    html += "\" maxlength=\"31\" oninput=\"updateWeatherTemplateHeading(";
    html += wi;
    html += ",this)\" required></div></div>";
    html += "<div class=\"hint-text\" style=\"margin-bottom:8px\">Ein Template kann mehrere Regeln enthalten. Reihenfolge im System: erst <b>Aussetzen</b>, danach <b>Verkürzen/Verlängern</b>. Zuschl&#228;ge und Abz&#252;ge beziehen sich immer auf die Basislaufzeit der Zuweisung.</div>";
    html += "<input type=\"hidden\" name=\"wt";
    html += wi;
    html += "_ruleCount\" id=\"wt";
    html += wi;
    html += "RuleCount\" value=\"";
    html += wt.ruleCount;
    html += "\">";
    html += "<div id=\"wtRules";
    html += wi;
    html += "\">";
    for (int ri = 0; ri < wt.ruleCount; ri++) {
        html += buildWeatherRuleRowHtml(wi, ri, wt.rules[ri]);
    }
    html += "</div>";
    html += "<div id=\"wt";
    html += wi;
    html += "NoRulesMsg\" class=\"hint-text\" style=\"display:";
    html += (wt.ruleCount == 0 ? "block" : "none");
    html += ";margin:8px 0\">Noch keine Wetterregel definiert.</div>";
    html += "<button type=\"button\" class=\"btn\" onclick=\"addWeatherRule(";
    html += wi;
    html += ")\" style=\"background:#5c88c8;padding:7px 14px;font-size:13px\">+ Regel hinzuf&#252;gen</button></div>";
    return html;
}

static String buildAssignmentRowsHtml(const SlotConfig& sc, const HardwareConfig& hw) {
    String html;
    int displayIdx = 0;
    for (int ai = 0; ai < sc.assignCount; ai++) {
        const SlotPumpAssignment& a = sc.assignments[ai];
        if (a.slotIndex >= (uint8_t)sc.slotCount) continue;
        if (a.pumpIndex >= (uint8_t)hw.relayCount) continue;
        html += "<div class=\"pump-entry\" id=\"asrow";
        html += displayIdx;
        html += "\" style=\"border:1px solid #eadfb7;padding:12px;margin-bottom:12px;border-radius:6px;background:#fffdf5\">";
        html += "<div style=\"display:flex;justify-content:space-between;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:10px\">";
        html += "<b style=\"font-size:1.05em\">🔗 Zuweisung ";
        html += displayIdx + 1;
        html += "</b>";
        html += "<div style=\"display:flex;gap:6px;flex-wrap:wrap\">";
        html += "<button type=\"button\" onclick=\"editAssignment(";
        html += displayIdx;
        html += ")\" style=\"padding:3px 8px;background:#17a2b8;color:#fff;border:none;border-radius:4px;cursor:pointer\">&#9998; Bearbeiten</button>";
        html += "<button type=\"button\" onclick=\"deleteAssignment(";
        html += displayIdx;
        html += ")\" style=\"padding:3px 8px;background:#dc3545;color:#fff;border:none;border-radius:4px;cursor:pointer\">&#10005; L&#246;schen</button>";
        html += "</div></div><div class=\"form-row\">";
        html += "<div class=\"form-col\"><label title=\"Ein Slot beschreibt nur, wann geprüft wird. Er enthält keine pumpenspezifische Wetterlogik.\">Slot</label><select name=\"as";
        html += displayIdx;
        html += "_slot\">";
        for (int si = 0; si < sc.slotCount; si++) {
            html += "<option value=\""; html += si; html += "\"";
            if (a.slotIndex == (uint8_t)si) html += " selected";
            html += ">";
            html += getSlotLabel(sc.slots[si], si);
            html += "</option>";
        }
        html += "</select></div>";

        html += "<div class=\"form-col\"><label title=\"Die konkrete Pumpe, die bei dieser Zuweisung laufen soll.\">Pumpe</label><select name=\"as";
        html += displayIdx;
        html += "_pump\">";
        for (int pi = 0; pi < hw.relayCount; pi++) {
            html += "<option value=\""; html += pi; html += "\"";
            if (a.pumpIndex == (uint8_t)pi) html += " selected";
            html += ">";
            html += getPumpLabel(hw, pi);
        html += "</option>";
        }
        html += "</select></div>";

        html += "<div class=\"form-col\"><label title=\"Hier wird festgelegt, welche Wetterregeln für genau diese Pumpe und diesen Slot gelten. Kein Template = reine Zeitsteuerung.\">Wetter-Template</label><select name=\"as";
        html += displayIdx;
        html += "_weatherTemplate\">";
        html += "<option value=\"-1\"";
        if (a.weatherTemplateIndex < 0) html += " selected";
        html += ">Kein Template</option>";
        for (int wi = 0; wi < sc.weatherTemplateCount; wi++) {
            html += "<option value=\"";
            html += wi;
            html += "\"";
            if (a.weatherTemplateIndex == wi) html += " selected";
            html += ">";
            html += getWeatherTemplateLabel(sc, wi);
            html += "</option>";
        }
        html += "</select></div>";

        html += "<div class=\"form-col\"><label title=\"Basislaufzeit ohne Wetteranpassung. Zuschläge und Abzüge der Regeln beziehen sich auf diesen Wert.\">Dauer (s)</label><input type=\"number\" name=\"as";
        html += displayIdx;
        html += "_duration\" value=\"";
        html += a.durationSec;
        html += "\" min=\"1\" max=\"7200\"></div>";
        html += "</div></div>";
        displayIdx++;
    }
    return html;
}

static String buildPumpSlotOverviewHtml(const SlotConfig& sc, const HardwareConfig& hw) {
    if (hw.relayCount == 0) {
        return "<p style='color:#999;font-style:italic'>Keine Pumpen konfiguriert.</p>";
    }
    String html = "<div class='table-wrap'><table class='compact-table'><tr><th>Pumpe</th><th>Zugewiesene Kombinationen</th></tr>";
    for (int pi = 0; pi < hw.relayCount; pi++) {
        html += "<tr><td>";
        html += getPumpLabel(hw, pi);
        html += "</td><td>";
        bool found = false;
        for (int ai = 0; ai < sc.assignCount; ai++) {
            const SlotPumpAssignment& a = sc.assignments[ai];
            if (a.pumpIndex != (uint8_t)pi || a.slotIndex >= (uint8_t)sc.slotCount) continue;
            if (found) html += "<br>";
            html += getSlotLabel(sc.slots[a.slotIndex], a.slotIndex);
            html += " · ";
            html += getWeatherTemplateLabel(sc, a.weatherTemplateIndex);
            html += " (";
            html += a.durationSec;
            html += "s)";
            found = true;
        }
        if (!found) html += "<span style='color:#999'>Keine Zuweisung</span>";
        html += "</td></tr>";
    }
    html += "</table></div>";
    return html;
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
                 "<p style='color:#1a6b3c;margin-top:8px'>&#10003; %d Slot(s), %d Wetter-Template(s), %d Zuweisung(en), %d Pumpe(n).</p>",
                 sc.slotCount, sc.weatherTemplateCount, sc.assignCount, hw.relayCount);
        wateringStatus = buf;
    } else {
        wateringStatus = "<p style='color:#dc3545;margin-top:8px'>&#10007; Kein g&#252;ltiger Plan: Pumpen konfigurieren, Slots anlegen und Zuweisungen speichern.</p>";
    }
    page = replaceToken(page, "{watering_status}", wateringStatus);

    // Pump names JSON array for JavaScript assignment editor
    {
        JsonDocument pumpNamesDoc;
        JsonArray arr = pumpNamesDoc.to<JsonArray>();
        for (int i = 0; i < hw.relayCount; i++) {
            arr.add(hw.pumps[i].name[0] ? String(hw.pumps[i].name)
                                        : ("Pumpe " + String(i + 1)));
        }
        String pumpNamesJson;
        serializeJson(arr, pumpNamesJson);
        page = replaceToken(page, "{pump_names_json}", pumpNamesJson);
    }
    // Slot names JSON array for assignment editor
    {
        JsonDocument slotNamesDoc;
        JsonArray arr = slotNamesDoc.to<JsonArray>();
        for (int i = 0; i < sc.slotCount; i++) {
            arr.add(getSlotLabel(sc.slots[i], i));
        }
        String slotNamesJson;
        serializeJson(arr, slotNamesJson);
        page = replaceToken(page, "{slot_names_json}", slotNamesJson);
    }
    {
        JsonDocument weatherTemplateNamesDoc;
        JsonArray arr = weatherTemplateNamesDoc.to<JsonArray>();
        for (int i = 0; i < sc.weatherTemplateCount; i++) {
            arr.add(getWeatherTemplateLabel(sc, i));
        }
        String weatherTemplateNamesJson;
        serializeJson(arr, weatherTemplateNamesJson);
        page = replaceToken(page, "{weather_template_names_json}", weatherTemplateNamesJson);
    }
    page = replaceToken(page, "{pumpCount}", String(hw.relayCount));
    page = replaceToken(page, "{slotCount}", String(sc.slotCount));
    page = replaceToken(page, "{weatherTemplateCount}", String(sc.weatherTemplateCount));
    page = replaceToken(page, "{assignCount}", String(sc.assignCount));

    // Build slot rows
    String slotRowsHtml;
    for (int i = 0; i < sc.slotCount; i++) {
        slotRowsHtml += buildSlotRowHtml(i, sc.slots[i], sc, hw);
    }
    String weatherTemplateRowsHtml;
    for (int i = 0; i < sc.weatherTemplateCount; i++) {
        weatherTemplateRowsHtml += buildWeatherTemplateRowHtml(i, sc.weatherTemplates[i]);
    }
    String assignmentRowsHtml = buildAssignmentRowsHtml(sc, hw);
    page = replaceToken(page, "{slot_rows_html}", slotRowsHtml);
    page = replaceToken(page, "{weather_template_rows_html}", weatherTemplateRowsHtml);
    page = replaceToken(page, "{assignment_rows_html}", assignmentRowsHtml);
    page = replaceToken(page, "{pump_assignment_overview_html}", buildPumpSlotOverviewHtml(sc, hw));
    page = replaceToken(page, "{noSlotsMsg}", sc.slotCount == 0 ? "block" : "none");
    page = replaceToken(page, "{noWeatherTemplatesMsg}", sc.weatherTemplateCount == 0 ? "block" : "none");
    page = replaceToken(page, "{noAssignmentsMsg}", sc.assignCount == 0 ? "block" : "none");

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
    int slotRemap[MAX_SLOTS];
    for (int i = 0; i < MAX_SLOTS; i++) slotRemap[i] = -1;

    for (int si = 0; si < slotCount; si++) {
        if (newSc.slotCount >= MAX_SLOTS) break;

        char key[32];
        snprintf(key, sizeof(key), "s%d_name", si);
        if (!g_server->hasArg(key)) continue;  // slot was deleted

        int mappedSlotIndex = newSc.slotCount;
        slotRemap[si] = mappedSlotIndex;
        WateringSlot& s = newSc.slots[mappedSlotIndex];
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

        snprintf(key, sizeof(key), "s%d_repeatMode", si);
        s.repeatMode = (uint8_t)constrain(
            g_server->hasArg(key) ? g_server->arg(key).toInt() : REPEAT_WEEKDAYS,
            REPEAT_WEEKDAYS, REPEAT_INTERVAL_DAYS);
        snprintf(key, sizeof(key), "s%d_intervalDays", si);
        s.intervalDays = (uint8_t)constrain(
            g_server->hasArg(key) ? g_server->arg(key).toInt() : 1, 1, 90);
        snprintf(key, sizeof(key), "s%d_intervalAnchor", si);
        if (g_server->hasArg(key)) {
            String ad = g_server->arg(key);
            if (ad.length() >= 10) {
                int y = ad.substring(0, 4).toInt();
                int m = ad.substring(5, 7).toInt();
                int d = ad.substring(8, 10).toInt();
                int epochDay = daysFromCivil(y, (unsigned)m, (unsigned)d);
                s.intervalAnchorDay = (uint16_t)constrain(epochDay, 0, 65535);
            } else {
                s.intervalAnchorDay = 0;
            }
        } else {
            s.intervalAnchorDay = 0;
        }
        if (s.repeatMode == REPEAT_INTERVAL_DAYS && s.intervalAnchorDay == 0) {
            time_t n = time(nullptr);
            if (n > 0) {
                struct tm nt;
                localtime_r(&n, &nt);
                s.intervalAnchorDay = (uint16_t)constrain(
                    daysFromCivil(nt.tm_year + 1900, (unsigned)(nt.tm_mon + 1), (unsigned)nt.tm_mday),
                    0, 65535);
            }
        }
        newSc.slotCount++;
    }

    int weatherTemplateCount = constrain(
        g_server->hasArg("weatherTemplateCount") ? g_server->arg("weatherTemplateCount").toInt() : 0,
        0, MAX_WEATHER_TEMPLATES);
    int templateRemap[MAX_WEATHER_TEMPLATES];
    for (int i = 0; i < MAX_WEATHER_TEMPLATES; i++) templateRemap[i] = -1;
    for (int wi = 0; wi < weatherTemplateCount; wi++) {
        if (newSc.weatherTemplateCount >= MAX_WEATHER_TEMPLATES) break;

        char key[32];
        snprintf(key, sizeof(key), "wt%d_name", wi);
        if (!g_server->hasArg(key)) continue;

        int mappedTemplateIndex = newSc.weatherTemplateCount;
        templateRemap[wi] = mappedTemplateIndex;
        WeatherTemplate& wt = newSc.weatherTemplates[mappedTemplateIndex];
        wt = WeatherTemplate{};
        strlcpy(wt.name, g_server->arg(key).c_str(), sizeof(wt.name));

        snprintf(key, sizeof(key), "wt%d_ruleCount", wi);
        int ruleCount = constrain(g_server->hasArg(key) ? g_server->arg(key).toInt() : 0,
                                  0, MAX_WEATHER_RULES_PER_TEMPLATE * 4);
        for (int ri = 0; ri < ruleCount && wt.ruleCount < MAX_WEATHER_RULES_PER_TEMPLATE; ri++) {
            char rkey[40];
            snprintf(rkey, sizeof(rkey), "wt%d_r%d_action", wi, ri);
            if (!g_server->hasArg(rkey)) continue;

            WeatherRule& rule = wt.rules[wt.ruleCount++];
            rule = WeatherRule{};
            snprintf(rkey, sizeof(rkey), "wt%d_r%d_enabled", wi, ri);
            rule.enabled = g_server->hasArg(rkey);
            snprintf(rkey, sizeof(rkey), "wt%d_r%d_action", wi, ri);
            rule.actionType = (uint8_t)constrain(g_server->arg(rkey).toInt(),
                                                 WEATHER_RULE_SKIP, WEATHER_RULE_INCREASE_RUNTIME);
            snprintf(rkey, sizeof(rkey), "wt%d_r%d_metric", wi, ri);
            rule.metric = (uint8_t)constrain(g_server->hasArg(rkey) ? g_server->arg(rkey).toInt() : WEATHER_METRIC_DAILY_RAIN_MM,
                                             WEATHER_METRIC_CURRENT_TEMP, WEATHER_METRIC_FORECAST_RAIN_PROB_MAX);
            snprintf(rkey, sizeof(rkey), "wt%d_r%d_operator", wi, ri);
            rule.comparison = (uint8_t)constrain(g_server->hasArg(rkey) ? g_server->arg(rkey).toInt() : WEATHER_OP_GTE,
                                                 WEATHER_OP_GT, WEATHER_OP_LTE);
            snprintf(rkey, sizeof(rkey), "wt%d_r%d_threshold", wi, ri);
            rule.threshold = g_server->hasArg(rkey) ? g_server->arg(rkey).toFloat() : 0.0f;
            snprintf(rkey, sizeof(rkey), "wt%d_r%d_windowHours", wi, ri);
            rule.windowHours = (uint8_t)constrain(g_server->hasArg(rkey) ? g_server->arg(rkey).toInt() : 24, 1, 48);
            snprintf(rkey, sizeof(rkey), "wt%d_r%d_effectPct", wi, ri);
            rule.effectPercent = (uint8_t)constrain(g_server->hasArg(rkey) ? g_server->arg(rkey).toInt() : 25, 1, 200);
        }
        newSc.weatherTemplateCount++;
    }

    int assignCount = constrain(
        g_server->hasArg("assignCount") ? g_server->arg("assignCount").toInt() : 0,
        0, MAX_SLOT_ASSIGNMENTS * 4);
    for (int ai = 0; ai < assignCount && newSc.assignCount < MAX_SLOT_ASSIGNMENTS; ai++) {
        char akey[32];
        snprintf(akey, sizeof(akey), "as%d_slot", ai);
        if (!g_server->hasArg(akey)) continue;
        int oldSlotIndex = g_server->arg(akey).toInt();
        if (oldSlotIndex < 0 || oldSlotIndex >= MAX_SLOTS) continue;
        int newSlotIndex = slotRemap[oldSlotIndex];
        if (newSlotIndex < 0 || newSlotIndex >= newSc.slotCount) continue;

        snprintf(akey, sizeof(akey), "as%d_pump", ai);
        if (!g_server->hasArg(akey)) continue;
        int pumpIndex = constrain(g_server->arg(akey).toInt(), 0, hw.relayCount > 0 ? hw.relayCount - 1 : 0);

        snprintf(akey, sizeof(akey), "as%d_duration", ai);
        int durationSec = g_server->hasArg(akey) ? constrain(g_server->arg(akey).toInt(), 1, 7200) : 60;

        SlotPumpAssignment& a = newSc.assignments[newSc.assignCount++];
        a.slotIndex = (uint8_t)newSlotIndex;
        a.pumpIndex = (uint8_t)pumpIndex;
        a.durationSec = durationSec;
        snprintf(akey, sizeof(akey), "as%d_weatherTemplate", ai);
        int oldTemplateIndex = g_server->hasArg(akey) ? g_server->arg(akey).toInt() : -1;
        a.weatherTemplateIndex =
            (oldTemplateIndex >= 0 && oldTemplateIndex < MAX_WEATHER_TEMPLATES)
                ? (int8_t)templateRemap[oldTemplateIndex]
                : (int8_t)-1;
        a.useOwnWeatherPolicy = false;
    }

    sc = newSc;
    cfg->saveSlotConfig();
    g_app->requestConfigApply();

    String page = buildPage(HTML_SAVED_LIVE);
    page = replaceToken(page, "{saved_back_url}", "/config_watering");
    g_server->send(200, "text/html; charset=UTF-8", page);
    Serial.printf("[Web] POST /save_watering – %d slot(s), %d weather template(s), %d assignment(s) saved.\n",
                  sc.slotCount, sc.weatherTemplateCount, sc.assignCount);
}

static time_t parseLocalDateTimeArg(const String& value) {
    if (value.length() < 16) return 0;
    struct tm t = {};
    t.tm_year = value.substring(0, 4).toInt() - 1900;
    t.tm_mon  = value.substring(5, 7).toInt() - 1;
    t.tm_mday = value.substring(8, 10).toInt();
    t.tm_hour = value.substring(11, 13).toInt();
    t.tm_min  = value.substring(14, 16).toInt();
    t.tm_sec  = 0;
    t.tm_isdst = -1;
    time_t ts = mktime(&t);
    return (ts == (time_t)-1) ? 0 : ts;
}

static String formatTimeHM(time_t ts) {
    if (ts <= 0) return "–";
    struct tm t;
    localtime_r(&ts, &t);
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    return String(buf);
}

static const char* actionToText(WateringDecisionAction action) {
    switch (action) {
        case WATER_ACTION_EXECUTE: return "execute";
        case WATER_ACTION_REDUCE:  return "reduce";
        case WATER_ACTION_EXTEND:  return "extend";
        case WATER_ACTION_FALLBACK:return "fallback";
        default:                   return "skip";
    }
}

static const char* actionToLabelDe(WateringDecisionAction action) {
    switch (action) {
        case WATER_ACTION_EXECUTE: return "ausführen";
        case WATER_ACTION_REDUCE:  return "verkürzen";
        case WATER_ACTION_EXTEND:  return "verlängern";
        case WATER_ACTION_FALLBACK:return "Fallback";
        default:                   return "aussetzen";
    }
}

static String formatDateTimeLocal(time_t ts) {
    if (ts <= 0) return "–";
    struct tm t;
    struct tm nowTm;
    time_t now = time(nullptr);
    localtime_r(&ts, &t);
    localtime_r(&now, &nowTm);
    char buf[24];
    if (t.tm_year != nowTm.tm_year) {
        snprintf(buf, sizeof(buf), "%02d.%02d.%04d %02d:%02d",
                 t.tm_mday, t.tm_mon + 1, t.tm_year + 1900, t.tm_hour, t.tm_min);
    } else {
        snprintf(buf, sizeof(buf), "%02d.%02d %02d:%02d",
                 t.tm_mday, t.tm_mon + 1, t.tm_hour, t.tm_min);
    }
    return String(buf);
}

static bool findPlanForPump(const WateringDecisionResult& res, int pumpIndex, WateringDecisionPumpPlan& outPlan) {
    for (int i = 0; i < res.planCount; i++) {
        if (res.plan[i].pumpIndex == (uint8_t)pumpIndex) {
            outPlan = res.plan[i];
            return true;
        }
    }
    return false;
}

static bool findNextSlotDecision(int slotIndex,
                                 time_t nowLocal,
                                 const SlotConfig& sc,
                                 const HardwareConfig& hw,
                                 const WeatherData* weatherData,
                                 bool weatherAvailable,
                                 bool weatherStale,
                                 NextSlotDecisionInfo& out) {
    out = NextSlotDecisionInfo{};
    if (slotIndex < 0 || slotIndex >= sc.slotCount) return false;
    if (nowLocal < 1000000L) return false;

    struct tm nowTm;
    localtime_r(&nowLocal, &nowTm);
    nowTm.tm_sec = 0;

    for (int dayOff = 0; dayOff <= MAX_NEXT_SEARCH_DAYS; dayOff++) {
        struct tm probeTm = nowTm;
        probeTm.tm_mday += dayOff;
        probeTm.tm_hour = 12;
        probeTm.tm_min  = 0;
        probeTm.tm_sec  = 0;
        probeTm.tm_isdst = -1;
        time_t dayProbe = mktime(&probeTm);
        if (dayProbe <= 0) continue;

        bool usedFallback = false;
        time_t trigger = WateringDecisionEngine::computeTriggerTime(
            sc.slots[slotIndex], dayProbe, weatherData, weatherAvailable, &usedFallback);
        (void)usedFallback;
        if (trigger <= nowLocal) continue;

        WateringDecisionInput in;
        in.slotConfig = &sc;
        in.hardwareConfig = &hw;
        in.weatherData = weatherData;
        in.weatherAvailable = weatherAvailable;
        in.weatherStale = weatherStale;
        in.nowLocal = trigger;
        in.slotIndex = slotIndex;
        in.enforceDayMatch = true;
        in.enforceTriggerMinute = true;
        WateringDecisionResult res = WateringDecisionEngine::evaluateSlot(in);
        if (!res.validInput || !res.dayMatched || !res.triggerMatched) continue;

        out.found = true;
        out.triggerTime = trigger;
        out.result = res;
        return true;
    }
    return false;
}

static void handleWateringTest() {
    ConfigManager*  cfg = g_app->getConfigManager();
    SlotConfig&     sc  = cfg->getSlotConfig();

    String page = buildPage(HTML_WATERING_TEST_PAGE);
    JsonDocument slotsDoc;
    JsonArray arr = slotsDoc.to<JsonArray>();
    for (int i = 0; i < sc.slotCount; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["idx"] = i;
        o["name"] = getSlotLabel(sc.slots[i], i);
    }
    String slotsJson;
    serializeJson(arr, slotsJson);
    page = replaceToken(page, "{slot_options_json}", slotsJson);
    g_server->send(200, "text/html; charset=UTF-8", page);
    Serial.println("[Web] GET /watering_test");
}

static void handleApiWateringSimulate() {
    if (g_server->method() != HTTP_POST) {
        g_server->send(405, "text/plain", "Method Not Allowed");
        return;
    }

    ConfigManager* cfg = g_app->getConfigManager();
    SlotConfig& sc = cfg->getSlotConfig();
    HardwareConfig& hw = cfg->getHardwareConfig();

    int slotIndex = g_server->hasArg("slotIndex") ? g_server->arg("slotIndex").toInt() : -1;
    time_t simNow = g_server->hasArg("simTime") ? parseLocalDateTimeArg(g_server->arg("simTime")) : 0;
    String weatherState = g_server->hasArg("weatherState") ? g_server->arg("weatherState") : "fresh";

    WeatherData simWeather = {};
    if (g_server->hasArg("temperature"))    simWeather.temperature   = g_server->arg("temperature").toFloat();
    if (g_server->hasArg("dailyPrecipMm"))  simWeather.dailyPrecipMm = g_server->arg("dailyPrecipMm").toFloat();
    if (g_server->hasArg("dailyPrecipPct")) simWeather.dailyPrecipPct= g_server->arg("dailyPrecipPct").toFloat();
    if (g_server->hasArg("precipMm"))       simWeather.precipMm      = g_server->arg("precipMm").toFloat();
    if (g_server->hasArg("precipProb"))     simWeather.precipProb    = g_server->arg("precipProb").toFloat();
    if (g_server->hasArg("tempMax"))        simWeather.tempMax       = g_server->arg("tempMax").toFloat();
    if (g_server->hasArg("tempMin"))        simWeather.tempMin       = g_server->arg("tempMin").toFloat();

    WeatherManager* wm = g_app->getWeatherManager();
    const WeatherData* weatherPtr = nullptr;
    bool weatherAvailable = false;
    bool weatherStale = false;

    if (weatherState == "live" && wm && wm->isAvailable()) {
        weatherPtr = &wm->getData();
        weatherAvailable = true;
        weatherStale = wm->isStale();
    } else if (weatherState == "unavailable") {
        weatherPtr = nullptr;
        weatherAvailable = false;
        weatherStale = false;
    } else {
        if (wm && wm->isAvailable()) {
            simWeather.sunrise = wm->getData().sunrise;
            simWeather.sunset  = wm->getData().sunset;
        }
        weatherPtr = &simWeather;
        weatherAvailable = true;
        weatherStale = (weatherState == "stale");
    }

    WateringDecisionInput in;
    in.slotConfig = &sc;
    in.hardwareConfig = &hw;
    in.weatherData = weatherPtr;
    in.weatherAvailable = weatherAvailable;
    in.weatherStale = weatherStale;
    in.nowLocal = simNow;
    in.slotIndex = slotIndex;
    in.enforceDayMatch = true;
    in.enforceTriggerMinute = true;

    WateringDecisionResult result = WateringDecisionEngine::evaluateSlot(in);

    JsonDocument doc;
    doc["ok"] = result.validInput;
    doc["slotIndex"] = result.slotIndex;
    doc["action"] = actionToText(result.action);
    doc["reason"] = result.reason;
    doc["weatherJustification"] = result.weatherJustification;
    doc["warnings"] = result.warnings;
    doc["triggerMatched"] = result.triggerMatched;
    doc["dayMatched"] = result.dayMatched;
    doc["triggerTime"] = formatTimeHM(result.triggerTime);
    doc["triggerSource"] = result.triggerSource;
    doc["totalDurationSec"] = result.totalDurationSec;
    doc["planCount"] = result.planCount;
    if (weatherPtr) {
        doc["sunrise"] = formatTimeHM(weatherPtr->sunrise);
        doc["sunset"] = formatTimeHM(weatherPtr->sunset);
        JsonArray h24 = doc["weather24h"].to<JsonArray>();
        for (int i = 0; i < weatherPtr->hourlyCount; i++) {
            JsonObject h = h24.add<JsonObject>();
            h["time"] = formatTimeHM(weatherPtr->hourlyTime[i]);
            h["temp"] = weatherPtr->hourlyTemp[i];
            h["precipMm"] = weatherPtr->hourlyPrecipMm[i];
            h["precipPct"] = weatherPtr->hourlyPrecipPct[i];
        }
    }
    JsonArray planArr = doc["plan"].to<JsonArray>();
    for (int i = 0; i < result.planCount; i++) {
        JsonObject p = planArr.add<JsonObject>();
        p["order"] = i + 1;
        p["pumpIndex"] = result.plan[i].pumpIndex;
        p["pumpName"] = getPumpLabel(hw, result.plan[i].pumpIndex);
        p["baseDurationSec"] = result.plan[i].baseDurationSec;
        p["durationSec"] = result.plan[i].durationSec;
        p["adjustmentPercent"] = result.plan[i].adjustmentPercent;
        p["action"] = actionToText(result.plan[i].action);
        p["reason"] = result.plan[i].reason;
        p["policySource"] = result.plan[i].policySource;
        p["appliedRules"] = result.plan[i].appliedRules;
    }

    String json;
    serializeJson(doc, json);
    g_server->send(200, "application/json", json);
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
        JsonArray h = doc["hourly24h"].to<JsonArray>();
        for (int i = 0; i < w.hourlyCount; i++) {
            JsonObject o = h.add<JsonObject>();
            o["ts"] = (long)w.hourlyTime[i];
            o["temp"] = w.hourlyTemp[i];
            o["precipMm"] = w.hourlyPrecipMm[i];
            o["precipPct"] = w.hourlyPrecipPct[i];
        }
    } else {
        doc["available"] = false;
    }
    if (wm) {
        doc["lastHttpCode"] = wm->getLastHttpCode();
        doc["lastError"] = wm->getLastError();
        doc["lastRequestUrl"] = wm->getLastRequestUrl();
        doc["heapFree"] = (uint32_t)ESP.getFreeHeap();
        doc["heapMin"] = (uint32_t)ESP.getMinFreeHeap();
        doc["heapTotal"] = (uint32_t)ESP.getHeapSize();
        doc["psramFree"] = (uint32_t)ESP.getFreePsram();
        doc["psramTotal"] = (uint32_t)ESP.getPsramSize();
    }
    String json;
    serializeJson(doc, json);
    g_server->send(200, "application/json", json);
}

static void handleApiWateringStatus() {
    ConfigManager* cfg = g_app->getConfigManager();
    SlotConfig& sc = cfg->getSlotConfig();
    HardwareConfig& hw = cfg->getHardwareConfig();
    RelayManager* rm = g_app->getRelayManager();
    WateringScheduler* sched = g_app->getScheduler();
    WeatherManager* wm = g_app->getWeatherManager();
    StateManager* sm = g_app->getStateManager();

    time_t now = time(nullptr);
    const WeatherData* weatherData = (wm && wm->isAvailable()) ? &wm->getData() : nullptr;
    bool weatherAvailable = (wm && wm->isAvailable());
    bool weatherStale = (wm && wm->isStale());

    JsonDocument doc;
    doc["ok"] = true;
    doc["nowEpoch"] = (long)now;
    doc["now"] = formatDateTimeLocal(now);
    doc["armed"] = cfg->isWateringConfigValid();
    doc["state"] = sm ? sm->getStateString() : "unknown";
    doc["schedulerBusy"] = sched ? sched->isBusy() : false;
    doc["activePump"] = sched ? sched->getActivePump() : -1;

    JsonObject weather = doc["weather"].to<JsonObject>();
    weather["available"] = weatherAvailable;
    weather["stale"] = weatherStale;
    if (weatherData) {
        weather["temperature"] = weatherData->temperature;
        weather["dailyPrecipMm"] = weatherData->dailyPrecipMm;
        weather["dailyPrecipPct"] = weatherData->dailyPrecipPct;
        weather["sunrise"] = formatTimeHM(weatherData->sunrise);
        weather["sunset"] = formatTimeHM(weatherData->sunset);
        weather["lastUpdate"] = formatDateTimeLocal(weatherData->lastUpdate);
    }

    JsonArray slotsArr = doc["slots"].to<JsonArray>();
    time_t nextGlobalTs = 0;
    String nextGlobalName = "";
    for (int si = 0; si < sc.slotCount; si++) {
        const WateringSlot& slot = sc.slots[si];
        NextSlotDecisionInfo next;
        findNextSlotDecision(si, now, sc, hw, weatherData, weatherAvailable, weatherStale, next);

        JsonObject s = slotsArr.add<JsonObject>();
        s["slotIndex"] = si;
        s["name"] = getSlotLabel(slot, si);
        s["enabled"] = slot.enabled;
        s["repeatMode"] = slot.repeatMode == REPEAT_INTERVAL_DAYS ? "interval" : "weekdays";
        s["repeatRule"] = describeRepeatRule(slot);
        s["nextTrigger"] = next.found ? formatDateTimeLocal(next.triggerTime) : String("–");
        s["nextAction"] = next.found ? actionToText(next.result.action) : "skip";
        s["reason"] = next.found ? String(next.result.reason) : String("Kein nächster Lauf gefunden.");
        s["triggerSource"] = next.found ? String(next.result.triggerSource) : String("");
        s["warnings"] = next.found ? String(next.result.warnings) : String("");

        JsonArray plan = s["plan"].to<JsonArray>();
        if (next.found) {
            for (int i = 0; i < next.result.planCount; i++) {
                JsonObject p = plan.add<JsonObject>();
                p["pumpIndex"] = next.result.plan[i].pumpIndex;
                p["pumpName"] = getPumpLabel(hw, next.result.plan[i].pumpIndex);
                p["action"] = actionToText(next.result.plan[i].action);
                p["durationSec"] = next.result.plan[i].durationSec;
                p["baseDurationSec"] = next.result.plan[i].baseDurationSec;
                p["adjustmentPercent"] = next.result.plan[i].adjustmentPercent;
                p["reason"] = next.result.plan[i].reason;
                p["policySource"] = next.result.plan[i].policySource;
                p["appliedRules"] = next.result.plan[i].appliedRules;
            }
        }

        if (next.found && (nextGlobalTs == 0 || next.triggerTime < nextGlobalTs)) {
            nextGlobalTs = next.triggerTime;
            nextGlobalName = getSlotLabel(slot, si);
        }
    }
    doc["nextSlot"] = nextGlobalTs > 0 ? nextGlobalName : String("–");
    doc["nextSlotTime"] = nextGlobalTs > 0 ? formatDateTimeLocal(nextGlobalTs) : String("–");

    JsonArray pumpsArr = doc["pumps"].to<JsonArray>();
    for (int pi = 0; pi < hw.relayCount; pi++) {
        RelayManager::PumpRuntimeInfo rt;
        bool haveRt = rm && rm->getPumpRuntimeInfo(pi, rt);
        JsonObject p = pumpsArr.add<JsonObject>();
        p["pumpIndex"] = pi;
        p["name"] = getPumpLabel(hw, pi);
        p["enabled"] = hw.pumps[pi].enabled;
        p["running"] = haveRt ? rt.running : false;
        p["lastStart"] = haveRt ? formatDateTimeLocal(rt.lastStartEpoch) : String("–");
        p["lastStop"] = haveRt ? formatDateTimeLocal(rt.lastStopEpoch) : String("–");
        p["status"] = haveRt ? String(rt.lastStatus) : String("unknown");

        bool foundPumpNext = false;
        time_t bestTs = 0;
        String bestSlot = "–";
        WateringDecisionPumpPlan bestPlan;
        WateringDecisionResult bestResult;
        for (int si = 0; si < sc.slotCount; si++) {
            NextSlotDecisionInfo next;
            if (!findNextSlotDecision(si, now, sc, hw, weatherData, weatherAvailable, weatherStale, next)) continue;
            WateringDecisionPumpPlan pp;
            if (!findPlanForPump(next.result, pi, pp)) continue;
            if (!foundPumpNext || next.triggerTime < bestTs) {
                foundPumpNext = true;
                bestTs = next.triggerTime;
                bestSlot = getSlotLabel(sc.slots[si], si);
                bestPlan = pp;
                bestResult = next.result;
            }
        }
        p["nextSlot"] = foundPumpNext ? bestSlot : String("–");
        p["nextTime"] = foundPumpNext ? formatDateTimeLocal(bestTs) : String("–");
        p["nextAction"] = foundPumpNext ? String(actionToText(bestPlan.action)) : String("skip");
        p["nextReason"] = foundPumpNext ? String(bestPlan.reason) : String("Keine Zuweisung.");
        p["nextDurationSec"] = foundPumpNext ? bestPlan.durationSec : 0;
        p["adjustmentPercent"] = foundPumpNext ? bestPlan.adjustmentPercent : 0;
        p["triggerSource"] = foundPumpNext ? String(bestResult.triggerSource) : String("");
        p["warnings"] = foundPumpNext ? String(bestResult.warnings) : String("");
        p["appliedRules"] = foundPumpNext ? String(bestPlan.appliedRules) : String("");
    }

    JsonArray warnings = doc["warnings"].to<JsonArray>();
    if (weatherStale) warnings.add("Wetterdaten sind veraltet.");
    if (!weatherAvailable) warnings.add("Wetterdaten sind nicht verfügbar.");
    if (!cfg->isWateringConfigValid()) warnings.add("Bewässerungsplan ist nicht scharf (armed=false).");

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
