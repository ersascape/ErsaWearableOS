#pragma once

#include "core/buttons.h"
#include "ersa/hal/display.h"

namespace AppNowPlaying {

void begin();
bool onButton(Buttons::Event event);
void render(ersa::hal::IDisplay& display);

} // namespace AppNowPlaying
