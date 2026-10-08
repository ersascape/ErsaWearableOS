#if defined(ARDUINO)

#include "drivers/rtc/ds3231/ds3231.h"

namespace ersa::drivers::rtc {
namespace {

uint8_t fromBcd(uint8_t value) {
    return static_cast<uint8_t>((value >> 4) * 10 + (value & 0x0f));
}

uint8_t toBcd(uint8_t value) {
    return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

} // namespace

Ds3231::Ds3231(TwoWire& wire, uint8_t address) : wire_(wire), address_(address) {}

bool Ds3231::begin(int sda, int scl, uint32_t frequencyHz) {
    wire_.begin(sda, scl);
    wire_.setClock(frequencyHz);
    wire_.setTimeOut(50);
    return probe();
}

bool Ds3231::probe() {
    wire_.beginTransmission(address_);
    return wire_.endTransmission() == 0;
}

bool Ds3231::read(uint16_t& year, uint8_t& month, uint8_t& day, uint8_t& hour,
                  uint8_t& minute, uint8_t& second, uint8_t& dayOfWeek) {
    uint8_t registers[7]{};
    if (!readRegisters(0x00, registers, sizeof(registers))) return false;
    second = fromBcd(registers[0] & 0x7f);
    minute = fromBcd(registers[1] & 0x7f);
    if (registers[2] & 0x40) {
        const uint8_t hour12 = fromBcd(registers[2] & 0x1f);
        hour = static_cast<uint8_t>((hour12 % 12) + ((registers[2] & 0x20) ? 12 : 0));
    } else {
        hour = fromBcd(registers[2] & 0x3f);
    }
    dayOfWeek = static_cast<uint8_t>(registers[3] & 0x07);
    day = fromBcd(registers[4] & 0x3f);
    month = fromBcd(registers[5] & 0x1f);
    year = static_cast<uint16_t>(2000 + fromBcd(registers[6]) + ((registers[5] & 0x80) ? 100 : 0));
    return true;
}

bool Ds3231::write(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
                   uint8_t minute, uint8_t second, uint8_t dayOfWeek) {
    if (year < 2000 || year > 2199 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour > 23 || minute > 59 || second > 59 || dayOfWeek < 1 || dayOfWeek > 7) return false;
    const uint8_t registers[7] = {
        toBcd(second), toBcd(minute), toBcd(hour), dayOfWeek,
        toBcd(day), static_cast<uint8_t>(toBcd(month) | (year >= 2100 ? 0x80 : 0)),
        toBcd(static_cast<uint8_t>(year % 100)),
    };
    if (!writeRegisters(0x00, registers, sizeof(registers))) return false;
    uint8_t status = 0;
    if (!readRegisters(0x0f, &status, 1)) return false;
    status = static_cast<uint8_t>(status & ~0x80);
    return writeRegisters(0x0f, &status, 1);
}

bool Ds3231::lostPower(bool& stopped) {
    uint8_t status = 0;
    if (!readRegisters(0x0f, &status, 1)) return false;
    stopped = (status & 0x80) != 0;
    return true;
}

bool Ds3231::readRegisters(uint8_t first, uint8_t* data, size_t size) {
    wire_.beginTransmission(address_);
    wire_.write(first);
    if (wire_.endTransmission(false) != 0) return false;
    if (wire_.requestFrom(static_cast<int>(address_), static_cast<int>(size)) != static_cast<int>(size)) return false;
    for (size_t i = 0; i < size; ++i) data[i] = static_cast<uint8_t>(wire_.read());
    return true;
}

bool Ds3231::writeRegisters(uint8_t first, const uint8_t* data, size_t size) {
    wire_.beginTransmission(address_);
    wire_.write(first);
    for (size_t i = 0; i < size; ++i) wire_.write(data[i]);
    return wire_.endTransmission() == 0;
}

} // namespace ersa::drivers::rtc

#endif
