#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include "StateManager.h"

class OledStatus {
public:
    OledStatus();
    bool begin();

    void showBoot();
    void showState(SystemState state);
    void showDs3231Status(bool present);
    void showWifiAttempt(int apIndex, int apCount, const char* ssid);
    void showWPS();
    void showApMode(const char* apSSID, const char* ip);
    void showNormal(const char* ip, const char* timeStr,
                    bool ds3231Missing, bool wateringLocked);
    void update(SystemState state, const String& ip, const String& apSSID);
    void showMessage(const char* line1,
                     const char* line2 = nullptr,
                     const char* line3 = nullptr);

private:
    U8G2_SSD1306_128X64_NONAME_F_HW_I2C _u8g2;
    bool          _initialized  = false;
    unsigned long _lastUpdate   = 0;
    static constexpr unsigned long UPDATE_INTERVAL_MS = 500;
};
