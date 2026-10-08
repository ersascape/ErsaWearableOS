#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ersa {
namespace protocols {

/**
 * Decode the Bluetooth SIG Current Time characteristic (UUID 0x2A2B).
 *
 * CTS reports local calendar fields rather than an absolute timezone-aware
 * instant. The returned epoch encodes those fields in a UTC-shaped number so
 * the watch RTC can preserve the wall-clock time shown by the phone. This
 * function validates the complete 10-byte field set and has no BLE dependency,
 * allowing malformed values and leap dates to be checked on a host.
 * @param value Pointer to the characteristic bytes.
 * @param size Must equal the specification's 10-byte structure size.
 * @param epoch Receives encoded seconds only on success; unchanged on failure.
 * @return False for null, wrong length, invalid calendar fields, or overflow.
 */
inline bool decodeCurrentTime(const uint8_t* value, size_t size, uint32_t& epoch) {
    if (!value || size != 10) return false;
    const uint16_t year = uint16_t(value[0]) | (uint16_t(value[1]) << 8);
    const uint8_t month = value[2], day = value[3];
    const uint8_t hour = value[4], minute = value[5], second = value[6];
    if (year < 2024 || year > 2099 || month < 1 || month > 12 ||
        hour > 23 || minute > 59 || second > 59 || value[7] > 7) return false;
    static constexpr uint8_t monthDays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    const uint8_t maxDay = monthDays[month - 1] + ((month == 2 && leap) ? 1 : 0);
    if (day < 1 || day > maxDay) return false;

    int y = year;
    const unsigned m = month;
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned mp = m > 2 ? m - 3 : m + 9;
    const unsigned doy = (153 * mp + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = int64_t(era) * 146097 + int64_t(doe) - 719468;
    const int64_t seconds = days * 86400 + hour * 3600 + minute * 60 + second;
    if (seconds < 0 || seconds > UINT32_MAX) return false;
    epoch = static_cast<uint32_t>(seconds);
    return true;
}

} // namespace protocols
} // namespace ersa
