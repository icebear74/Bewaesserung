#include "WeatherManager.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

WeatherManager::WeatherManager() {}

void WeatherManager::begin(ConfigManager* cfg) {
    _cfg      = cfg;
    _fetchDue = true;  // fetch as soon as WiFi is available
}

void WeatherManager::update() {
    if (!_cfg) return;

    // Trigger a fetch on first call after begin(), then on the hourly interval
    if (_fetchDue || (millis() - _lastFetchMs >= WEATHER_FETCH_INTERVAL_MS)) {
        if (WiFi.status() == WL_CONNECTED) {
            _fetchDue = false;
            fetchNow();
        }
    }
}

bool WeatherManager::fetchNow() {
    if (!_cfg || WiFi.status() != WL_CONNECTED) return false;

    DeviceConfig& dc = _cfg->getDeviceConfig();

    // Build Open-Meteo URL (HTTPS; no API key required)
    char url[512];
    snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast"
        "?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
        "precipitation,rain,snowfall,wind_speed_10m,wind_direction_10m,"
        "precipitation_probability"
        "&daily=sunrise,sunset,precipitation_sum,"
        "precipitation_probability_max,temperature_2m_max,temperature_2m_min"
        "&forecast_days=1&timezone=auto",
        dc.latitude, dc.longitude);

    // Use WiFiClientSecure; certificate verification is skipped because
    // embedding the full CA chain in firmware is impractical.  Traffic is
    // still encrypted via TLS, protecting against passive interception.
    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    HTTPClient http;
    http.begin(secureClient, url);
    http.setTimeout(WEATHER_HTTP_TIMEOUT_MS);
    int code = http.GET();
    if (code != 200) {
        Serial.printf("[Weather] HTTP error %d from Open-Meteo.\n", code);
        http.end();
        _lastFetchMs = millis();  // back off; don't retry immediately
        return false;
    }

    String body = http.getString();
    http.end();

    bool ok = parseResponse(body);
    _lastFetchMs = millis();
    return ok;
}

bool WeatherManager::isStale() const {
    if (!_data.available) return true;
    return (millis() - _lastFetchMs) > WEATHER_STALE_THRESHOLD_MS;
}

time_t WeatherManager::getMidday() const {
    if (_data.sunrise == 0 || _data.sunset == 0) return 0;
    return (_data.sunrise + _data.sunset) / 2;
}

// ─── Private helpers ──────────────────────────────────────────────────────────

bool WeatherManager::parseResponse(const String& body) {
    // Use a filter to keep memory usage low
    JsonDocument filter;
    filter["current"]["temperature_2m"]         = true;
    filter["current"]["apparent_temperature"]    = true;
    filter["current"]["relative_humidity_2m"]    = true;
    filter["current"]["precipitation"]           = true;
    filter["current"]["rain"]                    = true;
    filter["current"]["snowfall"]                = true;
    filter["current"]["wind_speed_10m"]          = true;
    filter["current"]["wind_direction_10m"]      = true;
    filter["current"]["precipitation_probability"] = true;
    filter["daily"]["sunrise"][0]                    = true;
    filter["daily"]["sunset"][0]                     = true;
    filter["daily"]["precipitation_sum"][0]          = true;
    filter["daily"]["precipitation_probability_max"][0] = true;
    filter["daily"]["temperature_2m_max"][0]         = true;
    filter["daily"]["temperature_2m_min"][0]         = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (err) {
        Serial.printf("[Weather] JSON parse error: %s\n", err.c_str());
        return false;
    }

    JsonObject cur = doc["current"];
    if (!cur.isNull()) {
        _data.temperature = cur["temperature_2m"]          | 0.0f;
        _data.feelsLike   = cur["apparent_temperature"]    | 0.0f;
        _data.humidity    = (float)(cur["relative_humidity_2m"] | 0);
        _data.precipMm    = cur["precipitation"]           | 0.0f;
        _data.rain        = cur["rain"]                    | 0.0f;
        _data.snow        = cur["snowfall"]                | 0.0f;
        _data.windSpeed   = cur["wind_speed_10m"]          | 0.0f;
        _data.windDir     = cur["wind_direction_10m"]      | 0.0f;
        _data.precipProb  = (float)(cur["precipitation_probability"] | 0);
    }

    JsonObject daily = doc["daily"];
    if (!daily.isNull()) {
        const char* srStr = daily["sunrise"][0]   | "";
        const char* ssStr = daily["sunset"][0]    | "";
        _data.sunrise      = parseIso8601Local(srStr);
        _data.sunset       = parseIso8601Local(ssStr);
        _data.dailyPrecipMm  = daily["precipitation_sum"][0]          | 0.0f;
        _data.dailyPrecipPct = (float)(daily["precipitation_probability_max"][0] | 0);
        _data.tempMax        = daily["temperature_2m_max"][0]         | 0.0f;
        _data.tempMin        = daily["temperature_2m_min"][0]         | 0.0f;
    }

    _data.lastUpdate = time(nullptr);
    _data.available  = true;
    Serial.printf("[Weather] Updated: %.1f°C, rain=%.1fmm, prob=%.0f%%, "
                  "sunrise=%ld, sunset=%ld\n",
                  _data.temperature, _data.dailyPrecipMm,
                  _data.dailyPrecipPct, (long)_data.sunrise, (long)_data.sunset);
    return true;
}

// Parse ISO 8601 local datetime string "YYYY-MM-DDTHH:MM" returned by
// Open-Meteo when timezone=auto.  The API returns LOCAL times, so we use
// mktime() which interprets the broken-down time in the current TZ (set by
// TimeSync::setTimezone / setenv("TZ",...)).
time_t WeatherManager::parseIso8601Local(const char* str) {
    if (!str || strlen(str) < 16) return 0;
    struct tm t = {};
    // sscanf is safe here; input is from trusted API with known fixed format
    int n = sscanf(str, "%d-%d-%dT%d:%d",
                   &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min);
    if (n < 5) return 0;
    t.tm_year  -= 1900;
    t.tm_mon   -= 1;
    t.tm_sec    = 0;
    t.tm_isdst  = -1;  // let mktime determine DST
    time_t result = mktime(&t);
    return (result == (time_t)-1) ? 0 : result;
}
