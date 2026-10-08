#pragma once

#if defined(ARDUINO)

#include "ersa/board/pins.h"

namespace ersa {
namespace hal {

class Esp32PinController {
public:
    static void apply(const board::Pins& pins);
};

} // namespace hal
} // namespace ersa

#endif // ARDUINO
