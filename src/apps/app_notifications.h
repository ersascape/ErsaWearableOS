#pragma once
#include "ersa/hal/display.h"
#include "core/buttons.h"

namespace AppNotifications {

void begin();
bool onButton(Buttons::Event event);
void render(ersa::hal::IDisplay& display, bool full = true);

} // namespace AppNotifications
