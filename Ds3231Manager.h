#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>

class Ds3231Manager {
public:
    Ds3231Manager();
    bool begin();                   // returns true if DS3231 detected
    bool isPresent()  const { return _present; }
    DateTime getTime();
    void setTime(time_t utc);
    void applyToSystemClock();      // sets ESP32 system time from DS3231
    float getTemperature();
    bool isRunning();

private:
    RTC_DS3231 _rtc;
    bool       _present = false;
};
