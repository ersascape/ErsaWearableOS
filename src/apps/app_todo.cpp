#include "app_todo.h"
#include "core/net_sync.h"
#include "core/debug_log.h"
#include "ersa/config/ui_strings.h"

namespace AppTodo {

namespace {
size_t selectedIndex = 0;
size_t topVisibleIndex = 0;
constexpr size_t VISIBLE_ITEMS = 4;

void drawRight(ersa::hal::IDisplay& display, const char* text, int16_t rightX, int16_t y, ersa::hal::FontFace font) {
    display.setFont(font);
    display.setTextSize(1);
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(rightX - int16_t(w) - x1, y);
    display.print(text);
}
} // namespace

void begin() {
    selectedIndex = 0;
    topVisibleIndex = 0;
}

bool onButton(Buttons::Event event) {
    const size_t total = NetSync::todoCount();
    if (total == 0) {
        if (event == Buttons::Event::Action || event == Buttons::Event::ActionLong) {
            NetSync::syncAll();
            return true;
        }
        return false;
    }

    if (event == Buttons::Event::Next) {
        // B1 = SCROLL down through tasks
        selectedIndex = (selectedIndex + 1) % total;
        if (selectedIndex >= topVisibleIndex + VISIBLE_ITEMS) {
            topVisibleIndex = selectedIndex - VISIBLE_ITEMS + 1;
        } else if (selectedIndex < topVisibleIndex) {
            topVisibleIndex = selectedIndex;
        }
        return true;
    } else if (event == Buttons::Event::Action) {
        // B2 = OK: Toggle the selected task
        NetSync::toggleTodo(selectedIndex);
        return true;
    } else if (event == Buttons::Event::ActionLong) {
        // B2 Hold: CalDAV sync
        NetSync::syncAll();
        return true;
    }
    return false;
}

void render(ersa::hal::IDisplay& display, bool full) {
    (void)full;
    display.fillScreen(0);   // Solid black
    display.setTextColor(1); // White

    // Clean lowercase header
    display.setFont(ersa::hal::FontFace::MiSansBold10);
    display.setCursor(18, 24);
    display.print(ersa::strings::APP_TITLE_TASKS);

    const size_t total = NetSync::todoCount();
    if (total > 0) {
        char countBuf[20];
        snprintf(countBuf, sizeof(countBuf), "%u tasks", unsigned(total));
        drawRight(display, countBuf, 184, 24, ersa::hal::FontFace::MiSansRegular8);
    } else {
        drawRight(display, NetSync::lastStatus(), 184, 24, ersa::hal::FontFace::MiSansRegular8);
    }

    // Clean minimal footer
    display.setFont(ersa::hal::FontFace::MiSansRegular8);
    display.setCursor(18, 186);
    display.print((total == 0) ? ersa::strings::NAV_TODO_EMPTY_FOOT : ersa::strings::NAV_TODO_FOOTER);

    display.setTextColor(1);

    if (total == 0) {
        display.setFont(ersa::hal::FontFace::MiSansRegular10);
        display.setCursor(18, 70);
        display.print(ersa::strings::MSG_NO_TASKS);

        display.setFont(ersa::hal::FontFace::MiSansRegular8);
        display.setCursor(18, 96);
        display.print(ersa::strings::MSG_PRESS_SYNC_CALDAV);
        return;
    }

    if (selectedIndex >= total) selectedIndex = 0;
    if (topVisibleIndex >= total) topVisibleIndex = 0;

    constexpr int16_t startY = 50;
    constexpr int16_t rowHeight = 26;

    for (size_t i = 0; i < VISIBLE_ITEMS; ++i) {
        const size_t idx = topVisibleIndex + i;
        if (idx >= total) break;

        const int16_t rowY = startY + i * rowHeight;
        const auto& item = NetSync::getTodo(idx);
        const bool isSelected = (idx == selectedIndex);

        char truncated[22];
        strncpy(truncated, item.title, sizeof(truncated) - 1);
        truncated[sizeof(truncated) - 1] = '\0';

        char lineBuf[28];
        snprintf(lineBuf, sizeof(lineBuf), "%s %s",
                 item.completed ? "[x]" : "[ ]",
                 truncated);

        if (isSelected) {
            display.fillRoundRect(14, rowY - 17, 168, 22, 4, 1);
            display.setTextColor(0); // Black text on white capsule
            display.setFont(ersa::hal::FontFace::MiSansBold8);
            display.setCursor(20, rowY - 1);
            display.print(lineBuf);
            display.setTextColor(1); // Reset White
        } else {
            display.setFont(ersa::hal::FontFace::MiSansRegular8);
            display.setCursor(20, rowY - 1);
            display.print(lineBuf);
        }
    }

    // Clean slender scroll indicator on right edge
    if (total > VISIBLE_ITEMS) {
        constexpr int16_t barX = 188;
        constexpr int16_t barY = 46;
        constexpr int16_t barH = 104;
        display.drawFastVLine(barX, barY, barH, 1);

        const int16_t thumbH = max(10, int((VISIBLE_ITEMS * barH) / total));
        const int16_t thumbY = barY + int((topVisibleIndex * (barH - thumbH)) / (total - VISIBLE_ITEMS));
        display.fillRect(barX - 1, thumbY, 3, thumbH, 1);
    }
}

} // namespace AppTodo
