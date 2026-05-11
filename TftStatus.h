#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include "StateManager.h"

// ST7735 colour-TFT status display (1.8″ panel, operated in landscape 160×128).
//
// Pinout defaults (matching the project's standard wiring):
//   CS  = 44  (TFT_CS)
//   RST = 43  (TFT_RST, or -1 to share the Arduino RESET line)
//   DC  =  4  (TFT_DC)
//   MOSI/CLK use the ESP32 default hardware-SPI pins (MOSI=23, CLK=18).
//
// Dashboard layout (running state):
//   0..25   Header  – current time HH:MM:SS (navy background)
//   26..43  Info    – IP address + system status text
//   44      Separator
//   46..99  Pumps   – 8 colour-coded badges (green=active, red=idle, grey=disabled)
//   100     Separator
//   102..127 Footer – current date

class TftStatus {
public:
    TftStatus(int csPin, int dcPin, int rstPin);
    bool begin();

    // Transitional screens (mirror the OledStatus API)
    void showBoot();
    void showMessage(const char* line1,
                     const char* line2 = nullptr,
                     const char* line3 = nullptr);
    void showDs3231Status(bool present);
    void showWifiAttempt(int apIndex, int apCount, const char* ssid);
    void showWPS();
    void showApMode(const char* apSSID, const char* ip);
    void showState(SystemState state);

    // Periodic dashboard update – call from Application::update().
    // pumpActive[i]  : pump i is currently running
    // pumpEnabled[i] : pump i is configured (enabled in hardware config)
    // pumpCount      : number of valid entries (0..MAX_RELAY_COUNT)
    void update(SystemState state, const String& ip, const String& apSSID,
                const bool* pumpActive, const bool* pumpEnabled, int pumpCount);

private:
    // ── RGB565 colour palette ─────────────────────────────────────────────────
    static constexpr uint16_t C_BLACK   = 0x0000;
    static constexpr uint16_t C_WHITE   = 0xFFFF;
    static constexpr uint16_t C_NAVY    = 0x000F;
    static constexpr uint16_t C_LGRAY   = 0xC618;
    static constexpr uint16_t C_DGRAY   = 0x39E7;
    static constexpr uint16_t C_GREEN   = 0x07E0;
    static constexpr uint16_t C_RED     = 0xF800;
    static constexpr uint16_t C_ORANGE  = 0xFC80;
    static constexpr uint16_t C_YELLOW  = 0xFFE0;

    // ── Landscape layout (160 × 128 px) ──────────────────────────────────────
    static constexpr int TFT_W   = 160;
    static constexpr int TFT_H   = 128;

    static constexpr int HDR_Y   = 0;    // header   0..25  (26 px – navy bg, time)
    static constexpr int HDR_H   = 26;
    static constexpr int INFO_Y  = 26;   // info    26..43  (18 px – IP + status)
    static constexpr int INFO_H  = 18;
    static constexpr int SEP1_Y  = 44;   // separator line
    static constexpr int PUMP_Y  = 46;   // pump bar 46..99  (54 px – 8 badges)
    static constexpr int PUMP_H  = 54;
    static constexpr int SEP2_Y  = 100;  // separator line
    static constexpr int FOOT_Y  = 102;  // footer 102..127  (26 px – date)
    static constexpr int FOOT_H  = 26;

    // 8 badges × 20 px = 160 px
    static constexpr int BADGE_W = 20;

    // ── Internal draw helpers ─────────────────────────────────────────────────
    void drawHeader(const char* timeStr);
    void drawInfoRow(const char* ip, const char* statusText, uint16_t statusColor);
    void drawPumpBar(const bool* active, const bool* enabled, int count);
    void drawFooter(const char* text);
    void drawSeparator(int y);
    void drawDashboard(const char* timeStr, const char* ip,
                       const char* statusText, uint16_t statusColor,
                       const char* footerText,
                       const bool* pumpActive, const bool* pumpEnabled,
                       int pumpCount);

    Adafruit_ST7735  _tft;
    bool             _initialized = false;
    unsigned long    _lastUpdate  = 0;
    static constexpr unsigned long UPDATE_INTERVAL_MS = 500;
};
