#include "app_agenda.h"
#include "core/net_sync.h"
#include "core/debug_log.h"
#include "ersa/config/ui_strings.h"
#include "ui/text_layout.h"
#include <Arduino.h>
#include <string.h>

namespace AppAgenda {

namespace {
size_t pageOffset = 0;
constexpr size_t PAGE_SIZE = 2;
constexpr int16_t LEFT = 18;
constexpr int16_t RIGHT = 182;

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
    pageOffset = 0;
}

bool onButton(Buttons::Event event) {
    const size_t total = NetSync::eventCount();
    if (event == Buttons::Event::Next) {
        if (total > PAGE_SIZE) {
            pageOffset = pageOffset + PAGE_SIZE < total ? pageOffset + PAGE_SIZE : 0;
            return true;
        }
        return false;
    } else if (event == Buttons::Event::Action || event == Buttons::Event::ActionLong) {
        // B2 = OK / ACTION: Trigger CalDAV sync
        NetSync::syncAll();
        return true;
    }
    return false;
}

void render(ersa::hal::IDisplay& display, const DateTime& now, bool full) {
    (void)full;
    display.fillScreen(0);   // Solid black
    display.setTextColor(1); // White
    const size_t total = NetSync::eventCount();
    if (pageOffset >= total) pageOffset = 0;

    // Keep the heading and date in separate, measured areas.
    display.setFont(ersa::hal::FontFace::MiSansBold10);
    display.setCursor(LEFT, 24);
    display.print(ersa::strings::APP_TITLE_AGENDA);

    static constexpr const char* DAYS[] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
    static constexpr const char* MONTHS[] = {
        "jan", "feb", "mar", "apr", "may", "jun",
        "jul", "aug", "sep", "oct", "nov", "dec"
    };
    char date[24];
    snprintf(date, sizeof(date), "%s %u %s", DAYS[now.dayOfTheWeek() % 7],
             unsigned(now.day()), MONTHS[(now.month() >= 1 && now.month() <= 12) ? now.month() - 1 : 0]);
    drawRight(display, date, RIGHT, 24, ersa::hal::FontFace::MiSansRegular8);
    display.drawFastHLine(LEFT, 33, RIGHT - LEFT, 1);

    if (total == 0) {
        display.setFont(ersa::hal::FontFace::MiSansBold10);
        display.setCursor(LEFT, 78);
        display.print(ersa::strings::MSG_NO_EVENTS_TODAY);

        display.setFont(ersa::hal::FontFace::MiSansRegular8);
        display.setCursor(LEFT, 101);
        display.print(ersa::strings::MSG_PRESS_SYNC_CALDAV);
        if (NetSync::lastStatus()[0] != '\0' && strcmp(NetSync::lastStatus(), "Ready") != 0) {
            WatchText::line(display, NetSync::lastStatus(), LEFT, 127, RIGHT - LEFT);
        }
    } else {
        const size_t pageEnd = (pageOffset + PAGE_SIZE < total) ? pageOffset + PAGE_SIZE : total;
        char pageLabel[20];
        snprintf(pageLabel, sizeof(pageLabel), "%u-%u / %u",
                 unsigned(pageOffset + 1), unsigned(pageEnd), unsigned(total));
        drawRight(display, pageLabel, RIGHT, 47, ersa::hal::FontFace::MiSansRegular8);

        constexpr int16_t ROW_TOPS[PAGE_SIZE] = {54, 108};
        for (size_t i = 0; i < PAGE_SIZE; ++i) {
            const size_t idx = pageOffset + i;
            if (idx >= total) break;
            const auto& ev = NetSync::getEvent(idx);

            display.setFont(ersa::hal::FontFace::MiSansBold8);
            display.setCursor(LEFT, ROW_TOPS[i] + 11);
            display.print(ev.timeStr[0] != '\0' ? ev.timeStr : "all day");
            display.setFont(ersa::hal::FontFace::MiSansRegular10);
            WatchText::line(display, ev.title, LEFT, ROW_TOPS[i] + 34, RIGHT - LEFT);
            display.drawFastHLine(LEFT, ROW_TOPS[i] + 44, RIGHT - LEFT, 1);
        }
    }

    display.setFont(ersa::hal::FontFace::MiSansRegular8);
    display.setCursor(LEFT, 186);
    display.print(total > PAGE_SIZE ? ersa::strings::NAV_AGENDA_FOOTER
                                    : ersa::strings::NAV_AGENDA_EMPTY_FOOT);
}

} // namespace AppAgenda
