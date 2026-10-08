#pragma once

#if defined(ARDUINO)

#include <Arduino.h>
#include <Wire.h>

namespace ersa::drivers::rtc {

/** Low-level DS3231 register transport. Calendar policy belongs to the RTC HAL. */
class Ds3231 {
public:
    /** Bind an I2C bus and its address without initializing hardware. */
    explicit Ds3231(TwoWire& wire = Wire, uint8_t address = 0x68);

    /** Start I2C with the selected board pins and verify the clock registers. */
    bool begin(int sda, int scl, uint32_t frequencyHz = 100000);
    /** Return true when the DS3231 responds to an I2C address probe. */
    bool probe();
    /** Read the seven calendar registers as binary year/month/day/time values. */
    bool read(uint16_t& year, uint8_t& month, uint8_t& day, uint8_t& hour,
              uint8_t& minute, uint8_t& second, uint8_t& dayOfWeek);
    /** Write a validated calendar snapshot to the seven clock registers. */
    bool write(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
               uint8_t minute, uint8_t second, uint8_t dayOfWeek);
    /** Read the oscillator-stop flag from the status register. */
    bool lostPower(bool& stopped);

private:
    bool readRegisters(uint8_t first, uint8_t* data, size_t size);
    bool writeRegisters(uint8_t first, const uint8_t* data, size_t size);

    TwoWire& wire_;
    uint8_t address_;
};

} // namespace ersa::drivers::rtc

#endif
