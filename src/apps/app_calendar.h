#pragma once
#include <RTClib.h>
#include "ersa/hal/display.h"
#include "core/buttons.h"

namespace AppCalendar {

void begin();
bool onButton(Buttons::Event event, const DateTime& now);
void render(ersa::hal::IDisplay& display, const DateTime& now);
void resetToCurrentMonth(const DateTime& now);

} // namespace AppCalendar
