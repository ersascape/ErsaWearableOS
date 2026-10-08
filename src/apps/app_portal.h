#pragma once
#include "ersa/hal/display.h"
#include "core/buttons.h"

namespace AppPortal {

void begin();
void stop();
void tick();
void render(ersa::hal::IDisplay& display);
bool onButton(Buttons::Event event);
bool isActive();

} // namespace AppPortal
