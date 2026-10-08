#pragma once
#include "ersa/hal/display.h"
#include "core/buttons.h"

namespace AppTodo {

void begin();
void render(ersa::hal::IDisplay& display, bool full = true);
bool onButton(Buttons::Event event);

} // namespace AppTodo
