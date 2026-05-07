#include "Pcf857xDevice.h"

bool Pcf857xDevice::begin(uint8_t address, uint8_t chipType, TwoWire* wire) {
    _wire = wire;
    _address = address;
    _chipType = chipType;
    _state = 0xFFFF;
    _initialized = (_wire != nullptr);
    if (!_initialized) return false;

    bool ok = writeState();
    _initialized = ok;
    return ok;
}

bool Pcf857xDevice::digitalWrite(uint8_t channel, bool level) {
    if (!_initialized || !_wire) return false;

    uint8_t maxChannel = (_chipType == EXPANDER_TYPE_PCF8575) ? 15 : 7;
    if (channel > maxChannel) return false;

    uint16_t mask = ((uint16_t)1U) << channel;
    if (level) {
        _state |= mask;
    } else {
        _state &= ~mask;
    }
    return writeState();
}

bool Pcf857xDevice::writeState() {
    if (!_wire) return false;

    _wire->beginTransmission(_address);
    _wire->write((uint8_t)(_state & 0xFF));
    if (_chipType == EXPANDER_TYPE_PCF8575) {
        _wire->write((uint8_t)((_state >> 8) & 0xFF));
    }

    return (_wire->endTransmission() == 0);
}
