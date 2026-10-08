#pragma once
#include "ersa/common/calendar_time.h"
#include "ersa/hal/display.h"
#include "core/buttons.h"

namespace AppCalendar {

void begin();
bool onButton(Buttons::Event event, const CalendarTime& now);
void render(ersa::hal::IDisplay& display, const CalendarTime& now);
void resetToCurrentMonth(const CalendarTime& now);

} // namespace AppCalendar
