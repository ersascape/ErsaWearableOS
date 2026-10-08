#pragma once

#include "ersa/common/types.h"
#include "ersa/events/event.h"

namespace ersa {
namespace hal {

/// Normalized board input events and button state.
class IInput {
public:
    virtual ~IInput() = default;

    /// Initialize configured input pins and event handling.
    virtual Result<void> init() = 0;
    /// Poll hardware and enqueue any resulting normalized input events.
    virtual void poll() = 0;
    /// Return whether the requested logical button is currently pressed.
    virtual bool isPressed(events::ButtonId button) const = 0;
};

} // namespace hal
} // namespace ersa
