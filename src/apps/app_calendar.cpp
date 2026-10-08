#include "app_calendar.h"
#include "core/debug_log.h"
#include "ersa/config/ui_strings.h"

namespace AppCalendar {

namespace {
uint16_t viewYear = 0;
uint8_t viewMonth = 0;
bool userInteracted = false;

const char* const monthNamesLower[] = {
    "january", "february", "march", "april", "may", "june",
    "july", "august", "september", "october", "november", "december"
};

const char* const dowHeadersLower[] = {
    "s", "m", "t", "w", "t", "f", "s"
};

uint8_t daysInMonth(uint16_t year, uint8_t month) {
    if (month == 2) {
        const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    if (month == 4 || month == 6 || month == 9 || month == 11) return 30;
    return 31;
}

void nextMonth() {
    if (++viewMonth > 12) {
        viewMonth = 1;
        if (++viewYear > 2099) viewYear = 2099;
    }
}

void prevMonth() {
    if (--viewMonth < 1) {
        viewMonth = 12;
        if (--viewYear < 2000) viewYear = 2000;
    }
}

void syncWithTime(const DateTime& now) {
    const uint16_t y = now.year();
    const uint8_t m = now.month();
    viewYear = (y >= 2000 && y <= 2099) ? y : 2026;
    viewMonth = (m >= 1 && m <= 12) ? m : 1;
}
} // namespace

void begin() {
    userInteracted = false;
    viewYear = 0;
    viewMonth = 0;
}

void resetToCurrentMonth(const DateTime& now) {
    syncWithTime(now);
    userInteracted = false;
    DebugLog::log("CAL reset to %04u-%02u", unsigned(viewYear), unsigned(viewMonth));
}

bool onButton(Buttons::Event event, const DateTime& now) {
    if (!userInteracted || viewYear == 0) {
        syncWithTime(now);
    }

    if (event == Buttons::Event::Next) {
        // B1 = SCROLL to next month
        nextMonth();
        userInteracted = true;
        DebugLog::log("CAL next month: %04u-%02u", unsigned(viewYear), unsigned(viewMonth));
        return true;
    } else if (event == Buttons::Event::Action || event == Buttons::Event::ActionLong) {
        // B2 = OK: Reset to current month (Today)
        resetToCurrentMonth(now);
        return true;
    }
    return false;
}

void render(ersa::hal::IDisplay& display, const DateTime& now) {
    if (!userInteracted || viewYear == 0) {
        syncWithTime(now);
    }

    if (viewMonth < 1) viewMonth = 1;
    if (viewMonth > 12) viewMonth = 12;
    if (viewYear < 2000) viewYear = 2000;
    if (viewYear > 2099) viewYear = 2099;

    display.fillScreen(0);   // Solid black
    display.setTextColor(1); // White

    // Clean lowercase header: e.g. "september 2026"
    char title[32];
    snprintf(title, sizeof(title), "%s %u", monthNamesLower[viewMonth - 1], unsigned(viewYear));
    display.setFont(ersa::hal::FontFace::MiSansBold10);
    display.setCursor(18, 24);
    display.print(title);

    // Weekday headers: s m t w t f s
    display.setFont(ersa::hal::FontFace::MiSansRegular8);
    constexpr int16_t startX = 16;
    constexpr int16_t colWidth = 24;

    for (uint8_t c = 0; c < 7; ++c) {
        int16_t x = startX + c * colWidth + 8;
        display.setCursor(x, 44);
        display.print(dowHeadersLower[c]);
    }

    // Days matrix
    DateTime firstDay(viewYear, viewMonth, 1, 0, 0, 0);
    const uint8_t startDow = firstDay.dayOfTheWeek();
    const uint8_t totalDays = daysInMonth(viewYear, viewMonth);

    const bool isCurrentMonth = (viewYear == now.year() && viewMonth == now.month());
    const uint8_t currentDay = now.day();

    constexpr int16_t startY = 64;
    constexpr int16_t rowHeight = 18;

    for (uint8_t d = 1; d <= totalDays; ++d) {
        const uint8_t slot = startDow + d - 1;
        const uint8_t col = slot % 7;
        const uint8_t row = slot / 7;

        const int16_t cellX = startX + col * colWidth;
        const int16_t cellY = startY + row * rowHeight;
        const bool isToday = (isCurrentMonth && d == currentDay);

        if (isToday) {
            display.fillRoundRect(cellX + 2, cellY - 12, 20, 16, 3, 1);
            display.setTextColor(0); // Black numeral on white badge
            display.setFont(ersa::hal::FontFace::MiSansBold8);
        } else {
            display.setTextColor(1);
            display.setFont(ersa::hal::FontFace::MiSansRegular8);
        }

        const int16_t numX = (d < 10) ? (cellX + 8) : (cellX + 4);
        display.setCursor(numX, cellY);
        display.print(d);
    }

    // Clean footer
    display.setTextColor(1);
    display.setFont(ersa::hal::FontFace::MiSansRegular8);
    display.setCursor(18, 186);
    display.print(ersa::strings::NAV_CALENDAR_FOOTER);
}

} // namespace AppCalendar
