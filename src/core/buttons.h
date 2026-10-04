#pragma once
#include <stdint.h>

namespace Buttons {
enum class Event : uint8_t {
    None = 0,
    Next,        // Top single click: Next screen
    Previous,    // Top double click: Previous screen
    Home,        // Top long press: Clock / Home screen
    Action,      // Bottom single click: Context action (e.g. +1 Month, select)
    ActionAlt,   // Bottom double click: Context alt action (e.g. reset month, toggle done)
    ActionLong   // Bottom long press: Context trigger (e.g. WiFi sync)
};

void begin();
void tick();
Event takeEvent();
bool isPressed();
bool bothPressed();
bool hasPendingEvents();
const char* name(Event event);
}
