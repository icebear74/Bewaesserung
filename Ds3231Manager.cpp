#include "Ds3231Manager.h"
#include <sys/time.h>

Ds3231Manager::Ds3231Manager() {}

bool Ds3231Manager::begin() {
    if (!_rtc.begin()) {
        Serial.println("[DS3231] Module not detected on I2C bus.");
        _present = false;
        return false;
    }
    _present = true;

    if (_rtc.lostPower()) {
        Serial.println("[DS3231] WARNING: RTC lost power - time may be invalid! "
                       "Will be corrected by NTP when available.");
    } else {
        DateTime now = _rtc.now();
        Serial.printf("[DS3231] Current time: %04d-%02d-%02d %02d:%02d:%02d\n",
                      now.year(), now.month(), now.day(),
                      now.hour(), now.minute(), now.second());
    }
    return true;
}

DateTime Ds3231Manager::getTime() {
    return _rtc.now();
}

void Ds3231Manager::setTime(time_t utc) {
    if (!_present) return;
    _rtc.adjust(DateTime((uint32_t)utc));
    Serial.printf("[DS3231] Time set to epoch %lu\n", (unsigned long)utc);
}

// Portable UTC mktime (not affected by local timezone setting)
static time_t utcTimegm(int year, int month, int day, int hour, int min, int sec) {
    static const int daysPerMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    long days = 0;
    for (int y = 1970; y < year; y++) {
        bool leap = ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
        days += leap ? 366 : 365;
    }
    bool leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
    for (int m = 1; m < month; m++) {
        days += daysPerMonth[m - 1];
        if (m == 2 && leap) days++;
    }
    days += day - 1;
    return (time_t)(days * 86400L + hour * 3600L + min * 60L + sec);
}

void Ds3231Manager::applyToSystemClock() {
    if (!_present) return;
    DateTime now = _rtc.now();
    // DS3231 stores UTC — use our own timegm to avoid mktime timezone issues
    time_t epoch = utcTimegm(now.year(), now.month(), now.day(),
                             now.hour(), now.minute(), now.second());
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
    Serial.printf("[DS3231] System clock set: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                  now.year(), now.month(), now.day(),
                  now.hour(), now.minute(), now.second());
}

float Ds3231Manager::getTemperature() {
    if (!_present) return 0.0f;
    return _rtc.getTemperature();
}

bool Ds3231Manager::isRunning() {
    if (!_present) return false;
    return !_rtc.lostPower();
}
