#include "app_drawer.h"
#include "core/debug_log.h"
#include "ersa/config/ui_strings.h"
#include <Arduino.h>

namespace AppDrawer {

namespace {
Item currentSelection = Item::NowPlaying;

const char* const labels[static_cast<size_t>(Item::Count)] = {
    ersa::strings::APP_TITLE_CLOCK,
    ersa::strings::APP_TITLE_NOW_PLAYING,
    ersa::strings::APP_TITLE_CALLS,
    ersa::strings::APP_TITLE_NOTIFS,
    ersa::strings::APP_TITLE_CALENDAR,
    ersa::strings::APP_TITLE_AGENDA,
    ersa::strings::APP_TITLE_TASKS,
    ersa::strings::APP_TITLE_HOTSPOT,
    ersa::strings::APP_TITLE_STATUS,
    "updater"
};

} // namespace

void begin() {
    currentSelection = Item::NowPlaying;
}

void next() {
    uint8_t val = static_cast<uint8_t>(currentSelection);
    val = (val + 1) % static_cast<uint8_t>(Item::Count);
    currentSelection = static_cast<Item>(val);
    DebugLog::log("DRAWER next selection: %u (%s)", val, labels[val]);
}

void previous() {
    uint8_t val = static_cast<uint8_t>(currentSelection);
    val = (val == 0) ? (static_cast<uint8_t>(Item::Count) - 1) : (val - 1);
    currentSelection = static_cast<Item>(val);
    DebugLog::log("DRAWER prev selection: %u (%s)", val, labels[val]);
}

Item selected() {
    return currentSelection;
}

void setSelected(Item item) {
    if (static_cast<uint8_t>(item) < static_cast<uint8_t>(Item::Count)) {
        currentSelection = item;
    }
}

void render(ersa::hal::IDisplay& display, bool full) {
    (void)full;
    display.fillScreen(0);   // Solid black
    display.setTextColor(1); // White

    // Clean left-aligned lowercase header
    display.setFont(ersa::hal::FontFace::MiSansBold10);
    display.setCursor(18, 22);
    display.print(ersa::strings::APP_DRAWER_HEADER);

    // Minimal footer without harsh dividing lines
    display.setFont(ersa::hal::FontFace::MiSansRegular8);
    display.setCursor(18, 190);
    display.print(ersa::strings::NAV_DRAWER_FOOTER);

    display.setTextColor(1);

    constexpr uint8_t visibleItems = 4;
    constexpr int16_t startY = 62;
    constexpr int16_t rowHeight = 29;
    const uint8_t selectedIndex = static_cast<uint8_t>(currentSelection);
    const uint8_t first = (selectedIndex / visibleItems) * visibleItems;
    const uint8_t candidateEnd = first + visibleItems;
    const uint8_t end = candidateEnd < static_cast<uint8_t>(Item::Count)
                            ? candidateEnd : static_cast<uint8_t>(Item::Count);

    for (uint8_t i = first; i < end; ++i) {
        const int16_t itemY = startY + (i - first) * rowHeight;
        const bool isSelected = (i == selectedIndex);

        if (isSelected) {
            display.fillRoundRect(10, itemY - 16, 180, 23, 4, 1);
            display.setTextColor(0); // Black text on white pill
            display.setFont(ersa::hal::FontFace::MiSansBold8);
            display.setCursor(24, itemY);
            display.print(labels[i]);
            display.setTextColor(1); // Reset White
        } else {
            display.setFont(ersa::hal::FontFace::MiSansRegular8);
            display.setCursor(24, itemY);
            display.print(labels[i]);
        }
    }

}

} // namespace AppDrawer
