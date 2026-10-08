#pragma once
#include "ersa/common/calendar_time.h"
#include "ersa/hal/display.h"

namespace WatchfaceClock {

void render(ersa::hal::IDisplay& display, const CalendarTime& time, bool full = true);

} // namespace WatchfaceClock
