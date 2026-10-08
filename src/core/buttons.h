#pragma once
#include <stdint.h>

namespace Buttons {
enum class Pull : uint8_t { None, Up, Down };
enum class Event : uint8_t {
    None = 0,
    Next,        // Top single click: Next screen
    Previous,    // Top double click: Previous screen
    Home,        // Top long press: Clock / Home screen
    Action,      // Bottom single click: Context action (e.g. +1 Month, select)
    ActionAlt,   // Bottom double click: Context alt action (e.g. reset month, toggle done)
    ActionLong   // Bottom long press: Context trigger (e.g. WiFi sync)
};

void begin(int topPin, int bottomPin, Pull topPull, Pull bottomPull,
           bool topActiveLow, bool bottomActiveLow);
void tick();
Event takeEvent();
bool hasPendingEvents();
const char* name(Event event);
}
