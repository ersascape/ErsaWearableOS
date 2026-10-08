#pragma once
#include "ersa/hal/display.h"
#include <string.h>

namespace WatchText {
// Measure the selected font instead of guessing character counts on a 200px display.
inline void line(ersa::hal::IDisplay& display, const char* text, int16_t x, int16_t y, uint16_t width) {
    char buffer[96];
    strncpy(buffer, text ? text : "", sizeof(buffer) - 4);
    buffer[sizeof(buffer) - 4] = 0;
    int16_t bx, by;
    uint16_t w, h;
    display.getTextBounds(buffer, 0, 0, &bx, &by, &w, &h);
    if (w > width) {
        size_t n = strlen(buffer);
        do {
            if (n == 0) break;
            --n;
            while (n && (uint8_t(buffer[n]) & 0xc0) == 0x80) --n;
            strcpy(buffer + n, "...");
            display.getTextBounds(buffer, 0, 0, &bx, &by, &w, &h);
        } while (w > width);
    }
    display.setCursor(x, y);
    display.print(buffer);
}
}
