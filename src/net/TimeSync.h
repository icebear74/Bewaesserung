#pragma once
#include <Arduino.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>
#include "core/ConfigManager.h"
#include "hw/Ds3231Manager.h"

#define NTP_UPDATE_INTERVAL_MS 3600000UL  // 1 hour

// ─── POSIX TZ string timezone converter ──────────────────────────────────────

class GeneralTimeConverter {
public:
    GeneralTimeConverter(const char* tzString = "UTC");
    bool setTimezone(const char* tzString);
    time_t toLocal(time_t utc_epoch) const;
    bool isDST(time_t utc_epoch) const;
    int getStdOffsetSec() const { return _stdOffsetSec; }

private:
    struct Rule { int month, week, day, hour; };

    bool parseTzString(const char* tz);
    time_t calculateRuleDate(int year, const Rule& rule, int offsetSec) const;
    static time_t myTimegm(struct tm* t);

    int  _stdOffsetSec = 0;
    int  _dstOffsetSec = 3600;
    Rule _dstStart     = {3, 5, 0, 2};
    Rule _dstEnd       = {10, 5, 0, 3};
    bool _hasDst       = false;
    bool _isValid      = false;
};

// ─── TimeSync ─────────────────────────────────────────────────────────────────

class TimeSync {
public:
    TimeSync();
    ~TimeSync();

    void begin(DeviceConfig& config, Ds3231Manager* ds3231);
    void update();

    bool   isSynced()          const { return _synced; }
    time_t getUtcTime()        const;
    time_t getLocalTime()      const;
    String getLocalTimeString() const;
    void   setTimezone(const char* tz);

private:
    WiFiUDP              _udp;
    NTPClient*           _ntpClient      = nullptr;
    GeneralTimeConverter* _timeConverter  = nullptr;
    Ds3231Manager*        _ds3231         = nullptr;
    bool                  _synced         = false;
    unsigned long         _lastSync       = 0;
    char                  _ntpServer[64]  = "pool.ntp.org";
};
