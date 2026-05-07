#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "ConfigManager.h"

class Pcf857xDevice {
public:
    bool begin(uint8_t address, uint8_t chipType, TwoWire* wire = &Wire);
    bool digitalWrite(uint8_t channel, bool level);

private:
    bool writeState();

    TwoWire* _wire       = nullptr;
    uint8_t  _address    = 0;
    uint8_t  _chipType   = EXPANDER_TYPE_PCF8574;
    uint16_t _state      = 0xFFFF;
    bool     _initialized = false;
};
