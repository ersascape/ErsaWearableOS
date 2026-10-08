#pragma once

#include "ersa/common/types.h"
#include <stdint.h>

namespace ersa {
namespace hal {

/// Board-level battery telemetry and sampling contract.
class IBattery {
public:
    virtual ~IBattery() = default;

    /// Initialize the battery measurement backend.
    virtual Result<void> init() = 0;
    /// Take a new battery sample and update cached measurements.
    virtual void sample() = 0;
    /// Return the latest measured voltage in millivolts.
    virtual uint16_t millivolts() const = 0;
    /// Return the estimated remaining capacity as a percentage from 0 to 100.
    virtual uint8_t percentage() const = 0;
    /// Report whether a usable battery is connected.
    virtual bool isConnected() const = 0;
    /// Report whether external charging power is present.
    virtual bool isCharging() const = 0;
};

} // namespace hal
} // namespace ersa
