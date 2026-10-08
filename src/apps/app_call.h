#pragma once

#include "core/buttons.h"
#include "ersa/hal/display.h"

namespace AppCall {

void begin();
bool onButton(Buttons::Event event);
void render(ersa::hal::IDisplay& display);
bool isCallActiveOrIncoming();

} // namespace AppCall
