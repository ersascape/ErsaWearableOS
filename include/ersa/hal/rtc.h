#pragma once

#include "ersa/common/types.h"
#include <stdint.h>

namespace ersa {
namespace hal {

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

/// Real-time clock contract using transport-neutral calendar values.
class IRtc {
public:
    virtual ~IRtc() = default;

    /// Initialize and validate the RTC backend.
    virtual Result<void> init() = 0;
    /// Read the current calendar time.
    virtual TimePoint now() = 0;
    /// Set the RTC calendar time.
    virtual Result<void> adjust(const TimePoint& time) = 0;
    /// Set the RTC from Unix epoch seconds.
    virtual Result<void> setEpoch(uint32_t epochSeconds) = 0;
    /// Return whether the RTC is initialized and reporting valid time.
    virtual bool isHealthy() const = 0;
};

} // namespace hal
} // namespace ersa
