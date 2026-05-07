#pragma once
#include <Arduino.h>
#include "ConfigManager.h"

// Interval for routine weather updates (1 hour)
#define WEATHER_FETCH_INTERVAL_MS (3600000UL)

// Data is considered stale after 2 hours without a successful fetch
#define WEATHER_STALE_THRESHOLD_MS (7200000UL)

// HTTP fetch timeout
#define WEATHER_HTTP_TIMEOUT_MS (10000)

class WeatherManager {
public:
    WeatherManager();

    void begin(ConfigManager* cfg);
    void update();           // call from main loop; auto-fetches when due
    bool fetchNow();         // force an immediate fetch (blocks briefly)

    const WeatherData& getData()    const { return _data; }
    bool  isAvailable()             const { return _data.available; }
    bool  isStale()                 const;

    // Astronomical helpers – return local epoch (0 if unavailable)
    time_t getSunrise() const { return _data.sunrise; }
    time_t getSunset()  const { return _data.sunset; }
    time_t getMidday()  const;

private:
    bool parseResponse(const String& body);
    static time_t parseIso8601Local(const char* str);

    ConfigManager* _cfg         = nullptr;
    WeatherData    _data;
    unsigned long  _lastFetchMs = 0;
    bool           _fetchDue    = true;  // fetch on first update() call
};
