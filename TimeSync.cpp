#include "TimeSync.h"
#include <WiFi.h>
#include <cstring>
#include <cstdlib>

// ─── GeneralTimeConverter implementation ──────────────────────────────────────

GeneralTimeConverter::GeneralTimeConverter(const char* tzString) {
    setTimezone(tzString);
}

bool GeneralTimeConverter::setTimezone(const char* tzString) {
    _isValid = false;
    _hasDst  = false;
    if (!tzString || strlen(tzString) == 0) return false;
    return parseTzString(tzString);
}

/*
 * Parse POSIX TZ string e.g. "CET-1CEST,M3.5.0,M10.5.0/3"
 * Format: STDoffset[DST[offset],start[/time],end[/time]]
 */
bool GeneralTimeConverter::parseTzString(const char* tz) {
    // Skip STD name (letters)
    const char* p = tz;
    while (*p && !(*p == '-' || *p == '+' || isdigit((unsigned char)*p))) p++;
    if (!*p) return false;

    // Parse STD offset (POSIX: negative = east of UTC, i.e. UTC+1 = -1)
    char* end;
    double stdHours = strtod(p, &end);
    _stdOffsetSec = (int)(-stdHours * 3600.0);  // invert sign: CET-1 → +3600
    p = end;

    if (!*p) {
        // No DST rule
        _hasDst  = false;
        _isValid = true;
        return true;
    }

    // Skip DST name
    while (*p && !(*p == ',' || *p == '-' || *p == '+' || isdigit((unsigned char)*p))) p++;

    // Optional DST offset
    if (*p == ',' || *p == '\0') {
        _dstOffsetSec = _stdOffsetSec + 3600;  // default: +1h
    } else {
        double dstHours = strtod(p, &end);
        _dstOffsetSec = (int)(-dstHours * 3600.0);
        p = end;
    }

    _hasDst = true;

    // Expect comma then start rule
    if (*p != ',') { _isValid = true; return true; }
    p++;

    // Parse Mm.w.d rule for DST start
    if (*p == 'M') {
        p++;
        _dstStart.month = strtol(p, &end, 10); p = end;
        if (*p == '.') p++;
        _dstStart.week  = strtol(p, &end, 10); p = end;
        if (*p == '.') p++;
        _dstStart.day   = strtol(p, &end, 10); p = end;
        _dstStart.hour  = 2;
        if (*p == '/') {
            p++;
            _dstStart.hour = strtol(p, &end, 10);
            p = end;
        }
    }

    if (*p != ',') { _isValid = true; return true; }
    p++;

    // Parse Mm.w.d rule for DST end
    if (*p == 'M') {
        p++;
        _dstEnd.month = strtol(p, &end, 10); p = end;
        if (*p == '.') p++;
        _dstEnd.week  = strtol(p, &end, 10); p = end;
        if (*p == '.') p++;
        _dstEnd.day   = strtol(p, &end, 10); p = end;
        _dstEnd.hour  = 2;
        if (*p == '/') {
            p++;
            _dstEnd.hour = strtol(p, &end, 10);
        }
    }

    _isValid = true;
    return true;
}

// myTimegm: portable timegm (UTC mktime) without locale
time_t GeneralTimeConverter::myTimegm(struct tm* t) {
    static const int days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int year = t->tm_year + 1900;
    long day  = 0;
    for (int y = 1970; y < year; y++) {
        day += 365 + (((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0) ? 1 : 0);
    }
    bool leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
    for (int m = 0; m < t->tm_mon; m++) {
        day += days[m];
        if (m == 1 && leap) day++;
    }
    day += t->tm_mday - 1;
    return (time_t)(day * 86400L + t->tm_hour * 3600L + t->tm_min * 60L + t->tm_sec);
}

// calculateRuleDate: find the UTC epoch for a DST transition rule in a given year
time_t GeneralTimeConverter::calculateRuleDate(int year, const Rule& rule, int offsetSec) const {
    // Find first occurrence of rule.day (0=Sun) in rule.month
    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon  = rule.month - 1;
    t.tm_mday = 1;
    t.tm_hour = rule.hour;
    t.tm_min  = 0;
    t.tm_sec  = 0;

    time_t base = myTimegm(&t);
    // day of week for 1st of month (UTC)
    struct tm* bt = gmtime(&base);
    int firstDow = bt->tm_wday;  // 0=Sun

    // Which occurrence?
    int targetDow = rule.day % 7;
    int diff = (targetDow - firstDow + 7) % 7;
    int mday = 1 + diff;

    if (rule.week == 5) {
        // Last occurrence: advance by 7 days while still in the same month
        static const int daysInMonth[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
        int maxDay = daysInMonth[rule.month];
        // Adjust for leap year in February
        if (rule.month == 2) {
            bool leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
            if (leap) maxDay = 29;
        }
        while (mday + 7 <= maxDay) mday += 7;
    } else {
        mday += (rule.week - 1) * 7;
    }

    t.tm_mday = mday;
    // Rule time is in local standard time, convert to UTC
    return myTimegm(&t) - offsetSec;
}

bool GeneralTimeConverter::isDST(time_t utc_epoch) const {
    if (!_hasDst || !_isValid) return false;

    struct tm* t = gmtime(&utc_epoch);
    int year = t->tm_year + 1900;

    time_t startUtc = calculateRuleDate(year, _dstStart, _stdOffsetSec);
    time_t endUtc   = calculateRuleDate(year, _dstEnd,   _stdOffsetSec + (_dstOffsetSec - _stdOffsetSec));

    if (startUtc < endUtc) {
        // Northern hemisphere: DST from start to end
        return (utc_epoch >= startUtc && utc_epoch < endUtc);
    } else {
        // Southern hemisphere: DST wraps around year
        return (utc_epoch >= startUtc || utc_epoch < endUtc);
    }
}

time_t GeneralTimeConverter::toLocal(time_t utc_epoch) const {
    if (!_isValid) return utc_epoch;
    return utc_epoch + (isDST(utc_epoch) ? _dstOffsetSec : _stdOffsetSec);
}

// ─── TimeSync implementation ──────────────────────────────────────────────────

TimeSync::TimeSync() {}

TimeSync::~TimeSync() {
    delete _ntpClient;
    delete _timeConverter;
}

void TimeSync::begin(DeviceConfig& config, Ds3231Manager* ds3231) {
    _ds3231 = ds3231;
    strlcpy(_ntpServer, config.ntpServer, sizeof(_ntpServer));

    _timeConverter = new GeneralTimeConverter(config.timezone);
    Serial.printf("[Time] Timezone: %s  stdOffset: %ds\n",
                  config.timezone, _timeConverter->getStdOffsetSec());

    // Set system timezone so localtime() works everywhere
    setenv("TZ", config.timezone, 1);
    tzset();

    _ntpClient = new NTPClient(_udp, _ntpServer, 0, NTP_UPDATE_INTERVAL_MS);
    _ntpClient->begin();

    // Try NTP sync with fallback servers
    const char* servers[] = {
        _ntpServer,
        "time.google.com",
        "time.cloudflare.com",
        "europe.pool.ntp.org"
    };

    for (int i = 0; i < 4 && !_synced; i++) {
        if (i > 0) {
            Serial.printf("[Time] Trying fallback NTP: %s\n", servers[i]);
            delete _ntpClient;
            _ntpClient = new NTPClient(_udp, servers[i], 0, NTP_UPDATE_INTERVAL_MS);
            _ntpClient->begin();
        }
        for (int attempt = 0; attempt < 3 && !_synced; attempt++) {
            if (_ntpClient->forceUpdate()) {
                _synced   = true;
                _lastSync = millis();
                // Set ESP32 system time
                time_t epoch = (time_t)_ntpClient->getEpochTime();
                struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
                settimeofday(&tv, nullptr);
                Serial.printf("[Time] NTP sync OK: %s (UTC epoch %lu)\n",
                              getLocalTimeString().c_str(), (unsigned long)epoch);
                // Update DS3231 from NTP
                if (_ds3231 && _ds3231->isPresent()) {
                    _ds3231->setTime(epoch);
                    Serial.println("[Time] DS3231 updated from NTP.");
                }
            } else {
                delay(500);
            }
        }
    }

    if (!_synced) {
        Serial.println("[Time] NTP sync failed.");
    }
}

void TimeSync::update() {
    if (!_ntpClient) return;

    unsigned long now = millis();
    if (_synced && (now - _lastSync) < NTP_UPDATE_INTERVAL_MS) return;

    if (_ntpClient->update()) {
        time_t epoch = (time_t)_ntpClient->getEpochTime();
        struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
        settimeofday(&tv, nullptr);
        _lastSync = now;
        _synced   = true;
        if (_ds3231 && _ds3231->isPresent()) {
            _ds3231->setTime(epoch);
        }
        Serial.printf("[Time] Periodic NTP sync OK: %s\n", getLocalTimeString().c_str());
    }
}

time_t TimeSync::getUtcTime() const {
    time_t now;
    ::time(&now);
    return now;
}

time_t TimeSync::getLocalTime() const {
    if (!_timeConverter) return getUtcTime();
    return _timeConverter->toLocal(getUtcTime());
}

String TimeSync::getLocalTimeString() const {
    time_t utc = getUtcTime();
    struct tm t_buf;
    struct tm* t = localtime_r(&utc, &t_buf);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
    return String(buf);
}

void TimeSync::setTimezone(const char* tz) {
    if (_timeConverter) {
        _timeConverter->setTimezone(tz);
        setenv("TZ", tz, 1);
        tzset();
        Serial.printf("[Time] Timezone updated: %s\n", tz);
    }
}
