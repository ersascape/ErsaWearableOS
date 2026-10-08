#pragma once
#include <RTClib.h>
#include "ersa/hal/display.h"

namespace WatchfaceClock {

void render(ersa::hal::IDisplay& display, const DateTime& time, bool full = true);

} // namespace WatchfaceClock
