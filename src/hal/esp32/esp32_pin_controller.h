#pragma once

#if defined(ARDUINO)

#include "ersa/board/pins.h"

namespace ersa {
namespace hal {

/**
 * ESP32 pinctrl adapter that applies a generic board pin description.
 * Board-specific numbers remain in the BSP; this class translates mux, pull,
 * direction, and active-level metadata into ESP32 GPIO/peripheral setup.
 */
class Esp32PinController {
public:
    /** Apply all supported pin groups before dependent drivers initialize. */
    static void apply(const board::Pins& pins);
};

} // namespace hal
} // namespace ersa

#endif // ARDUINO
