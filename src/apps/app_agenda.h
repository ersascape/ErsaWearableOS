#pragma once
#include "ersa/hal/display.h"
#include <RTClib.h>
#include "core/buttons.h"

namespace AppAgenda {

void begin();
void render(ersa::hal::IDisplay& display, const DateTime& now, bool full = true);
bool onButton(Buttons::Event event);

} // namespace AppAgenda
