#pragma once
#include "ersa/hal/display.h"
#include "ersa/common/calendar_time.h"
#include "core/buttons.h"

namespace AppAgenda {

void begin();
void render(ersa::hal::IDisplay& display, const CalendarTime& now, bool full = true);
bool onButton(Buttons::Event event);

} // namespace AppAgenda
