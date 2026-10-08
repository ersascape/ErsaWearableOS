#pragma once
#include "ersa/hal/display.h"
#include "core/buttons.h"

namespace AppStatus {

void render(ersa::hal::IDisplay& display);
bool onButton(Buttons::Event event);

} // namespace AppStatus
