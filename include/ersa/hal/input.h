#pragma once

#include "ersa/common/types.h"
#include "ersa/events/event.h"

namespace ersa {
namespace hal {

/**
 * Normalized board input events and button state.
 *
 * The BSP supplies physical pin assignments and polarity; this interface
 * exposes product-relative buttons and normalized events. As a result, apps
 * never need to poll GPIO numbers or know whether a switch is active low.
 */
class IInput {
public:
    virtual ~IInput() = default;

    /** Configure board pins, debounce state, and event publication. */
    virtual Result<void> init() = 0;
    /**
     * Sample/debounce physical controls and post resulting events.
     * Call regularly from the application task; hardware callbacks should not
     * call app code directly.
     */
    virtual void poll() = 0;
    /** Query current debounced state using a logical ButtonId. */
    virtual bool isPressed(events::ButtonId button) const = 0;
    /** Report queued normalized input events that still need application dispatch. */
    virtual bool hasPendingEvents() const = 0;
};

} // namespace hal
} // namespace ersa
