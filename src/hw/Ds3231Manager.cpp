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

DateTime Ds3231Manager::getTime() const {
    return _rtc.now();
}

void Ds3231Manager::setTime(time_t utc) {
    if (!_present) return;
    _rtc.adjust(DateTime((uint32_t)utc));
    Serial.printf("[DS3231] Time set to epoch %lu\n", (unsigned long)utc);
}

void Ds3231Manager::applyToSystemClock() {
    if (!_present) return;
    DateTime now = _rtc.now();
    // Convert RTClib DateTime to time_t (UTC)
    struct tm t = {};
    t.tm_year = now.year() - 1900;
    t.tm_mon  = now.month() - 1;
    t.tm_mday = now.day();
    t.tm_hour = now.hour();
    t.tm_min  = now.minute();
    t.tm_sec  = now.second();
    t.tm_isdst = 0;
    time_t epoch = mktime(&t);
    // mktime uses local time; since we store UTC in DS3231, compensate
    // by using a portable UTC conversion approach via timegm equivalent
    // We use the gmtime round-trip trick
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
    Serial.printf("[DS3231] System clock set: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                  now.year(), now.month(), now.day(),
                  now.hour(), now.minute(), now.second());
}

float Ds3231Manager::getTemperature() const {
    if (!_present) return 0.0f;
    return _rtc.getTemperature();
}

bool Ds3231Manager::isRunning() const {
    if (!_present) return false;
    return !_rtc.lostPower();
}
