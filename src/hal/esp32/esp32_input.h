#pragma once

#if defined(ARDUINO)

#include "ersa/hal/input.h"
#include "ersa/events/event_bus.h"

namespace ersa {
namespace hal {

/**
 * ESP32 GPIO input adapter that normalizes physical switches to button events.
 * Pin polarity and button identity are provided by the BSP; this class handles
 * sampling/debounce so application behavior does not depend on GPIO APIs.
 */
class Esp32Input : public IInput {
public:
    /** Bind logical top/bottom GPIO numbers and the event destination. */
    Esp32Input(int topPin, int bottomPin, events::EventBus& bus = events::EventBus::instance());
    ~Esp32Input() override = default;

    Result<void> init() override;
    void poll() override;
    bool isPressed(events::ButtonId button) const override;

private:
    int topPin_, bottomPin_;
    events::EventBus& bus_;
};

} // namespace hal
} // namespace ersa

#endif // ARDUINO
