#pragma once

#include "ersa/common/types.h"
#include <stdint.h>

namespace ersa {
namespace hal {

/**
 * Transport-neutral local calendar snapshot and its epoch representation.
 * The firmware stores the phone's displayed wall time in the RTC; consumers
 * should use TimeService to apply source priority and validation rather than
 * writing this structure directly to a device.
 */
struct TimePoint {
    uint16_t year{2026};
    uint8_t month{9};
    uint8_t day{28};
    uint8_t hour{0};
    uint8_t minute{0};
    uint8_t second{0};
    uint8_t dayOfWeek{1}; // 0 = Sunday, 1 = Monday, ... 6 = Saturday
    uint32_t epoch{0};

    constexpr TimePoint() = default;
    constexpr TimePoint(uint16_t y, uint8_t m, uint8_t d, uint8_t h, uint8_t min, uint8_t s, uint8_t dow = 0, uint32_t ep = 0)
        : year(y), month(m), day(d), hour(h), minute(min), second(s), dayOfWeek(dow), epoch(ep) {}
};

/** Real-time clock contract using transport-neutral calendar values. */
class IRtc {
public:
    virtual ~IRtc() = default;

    /** Initialize the chip/bus and verify that calendar reads are plausible. */
    virtual Result<void> init() = 0;
    /** Return a calendar snapshot; the implementation may derive epoch fields. */
    virtual TimePoint now() = 0;
    /** Write explicit calendar fields and report bus/chip failures. */
    virtual Result<void> adjust(const TimePoint& time) = 0;
    /** Convert and write epoch seconds without exposing a chip-specific date type. */
    virtual Result<void> setEpoch(uint32_t epochSeconds) = 0;
    /** Report backend health; this does not guarantee the wall time is accurate. */
    virtual bool isHealthy() const = 0;
};

} // namespace hal
} // namespace ersa
