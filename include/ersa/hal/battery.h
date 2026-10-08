#pragma once

#include "ersa/common/types.h"
#include <stdint.h>

namespace ersa {
namespace hal {

/**
 * Board-level battery telemetry and sampling contract.
 *
 * Sampling is explicit so PowerManager can adapt its cadence to battery health
 * instead of performing ADC work on every UI tick. Implementations cache the
 * most recent measurement; getters therefore return a coherent last sample
 * without initiating potentially expensive hardware conversions.
 */
class IBattery {
public:
    virtual ~IBattery() = default;

    /** Configure ADC/gauge resources and report whether the backend is usable. */
    virtual Result<void> init() = 0;
    /** Refresh cached telemetry; implementations should keep this bounded. */
    virtual void sample() = 0;
    /// Return the latest measured voltage in millivolts.
    virtual uint16_t millivolts() const = 0;
    /** Return a bounded estimate; voltage-only boards must not imply precision. */
    virtual uint8_t percentage() const = 0;
    /// Report whether a usable battery is connected.
    virtual bool isConnected() const = 0;
    /// Report whether external charging power is present.
    virtual bool isCharging() const = 0;
};

} // namespace hal
} // namespace ersa
