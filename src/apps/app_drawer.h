#pragma once
#include "ersa/hal/display.h"

namespace AppDrawer {

enum class Item : uint8_t {
    Clock = 0,
    NowPlaying,
    Calls,
    Notifications,
    Calendar,
    Agenda,
    Todo,
    Hotspot,
    Status,
    Updater,
    Count
};

void begin();
void next();
void previous();
Item selected();
void setSelected(Item item);
void render(ersa::hal::IDisplay& display, bool full = true);

} // namespace AppDrawer
