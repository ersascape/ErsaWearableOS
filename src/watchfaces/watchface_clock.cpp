#include "watchface_clock.h"
#include "core/watch_clock.h"
#include "ersa/services/power_manager.h"
#include "core/net_sync.h"
#include "ersa/config/ui_strings.h"
#include "ersa/services/bluetooth_manager.h"

namespace WatchfaceClock {

namespace {

const char* const hoursWords[] = {
    "twelve", "one", "two", "three", "four", "five",
    "six", "seven", "eight", "nine", "ten", "eleven", "twelve"
};

const char* const onesWords[] = {
    "", "one", "two", "three", "four", "five",
    "six", "seven", "eight", "nine"
};

const char* const teensWords[] = {
    "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen",
    "sixteen", "seventeen", "eighteen", "nineteen"
};

const char* const tensWords[] = {
    "", "", "twenty", "thirty", "forty", "fifty"
};

const char* const daysLower[] = {
    "sunday", "monday", "tuesday", "wednesday",
    "thursday", "friday", "saturday"
};

const char* const monthsLower[] = {
    "january", "february", "march", "april", "may", "june",
    "july", "august", "september", "october", "november", "december"
};

const char* getOrdinalSuffix(uint8_t day) {
    if (day >= 11 && day <= 13) return "th";
    switch (day % 10) {
        case 1: return "st";
        case 2: return "nd";
        case 3: return "rd";
        default: return "th";
    }
}

void timeToWords(uint8_t hour, uint8_t min, const char*& line1, const char*& line2, const char*& line3) {
    uint8_t h12 = hour % 12;
    if (h12 == 0) h12 = 12;
    line1 = hoursWords[h12];
    line2 = "";
    line3 = "";

    if (min == 0) {
        line2 = "o'clock";
    } else if (min < 10) {
        line2 = "oh";
        line3 = onesWords[min];
    } else if (min < 20) {
        line2 = teensWords[min - 10];
    } else {
        line2 = tensWords[min / 10];
        if (min % 10 != 0) {
            line3 = onesWords[min % 10];
        }
    }
}

void drawRightAlignedText(ersa::hal::IDisplay& display, const char* text, int16_t rightX, int16_t y, ersa::hal::FontFace font) {
    display.setFont(font);
    display.setTextSize(1);
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(rightX - int16_t(w) - x1, y);
    display.print(text);
}

void drawMusicNote(ersa::hal::IDisplay& display, int16_t x, int16_t y) {
    // Compact double eighth-note mark, independent of font glyph coverage.
    display.fillCircle(x + 2, y + 12, 2, 1);
    display.fillCircle(x + 8, y + 9, 2, 1);
    display.drawLine(x + 4, y + 12, x + 4, y + 1, 1);
    display.drawLine(x + 4, y + 1, x + 10, y + 1, 1);
    display.drawLine(x + 10, y + 1, x + 10, y + 9, 1);
}

} // namespace

void render(ersa::hal::IDisplay& display, const DateTime& time, bool full) {
    (void)full;
    // 1. Full solid black canvas
    display.fillScreen(0);      // 0 = GxEPD_BLACK

    // 3. Bottom-Right Date: e.g. "thursday" / "november 12th, 2020"
    const uint8_t dow = time.dayOfTheWeek() % 7;
    const uint8_t mon = (time.month() >= 1 && time.month() <= 12) ? (time.month() - 1) : 0;

    char dateBuf[36];
    snprintf(dateBuf, sizeof(dateBuf), "%s %u%s, %u",
             monthsLower[mon], unsigned(time.day()),
             getOrdinalSuffix(time.day()), unsigned(time.year()));

    // 3a. Above Date Complication: Call status or Now Playing metadata
    auto& bleMgr = ersa::services::BluetoothManager::instance();
    auto callState = bleMgr.getCallState();
    if (callState == ersa::services::CallState::Incoming) {
        char callBuf[32];
        const char* caller = bleMgr.getCallerName();
        snprintf(callBuf, sizeof(callBuf), "call: %s", (caller && caller[0]) ? caller : "unknown");
        if (strlen(callBuf) > 20) {
            callBuf[17] = '.'; callBuf[18] = '.'; callBuf[19] = '.'; callBuf[20] = '\0';
        }
        drawRightAlignedText(display, callBuf, 184, 150, ersa::hal::FontFace::MiSansBold8);
    } else if (callState == ersa::services::CallState::Active) {
        char callBuf[32];
        uint32_t sec = bleMgr.getCallDurationSec();
        snprintf(callBuf, sizeof(callBuf), "in call %02u:%02u", unsigned(sec / 60), unsigned(sec % 60));
        drawRightAlignedText(display, callBuf, 184, 150, ersa::hal::FontFace::MiSansBold8);
    } else {
        const char* title = bleMgr.getMediaTitle();
        if (title && title[0] != '\0' && strcmp(title, "No Media") != 0) {
            char mediaBuf[32];
            snprintf(mediaBuf, sizeof(mediaBuf), "%s", title);
            // Reserve a generous left inset and leave the note clear on the right.
            display.setFont(ersa::hal::FontFace::MiSansRegular8);
            int16_t boundsX, boundsY;
            uint16_t textWidth, textHeight;
            display.getTextBounds(mediaBuf, 0, 0, &boundsX, &boundsY, &textWidth, &textHeight);
            while (textWidth > 114 && strlen(mediaBuf) > 4) {
                const size_t length = strlen(mediaBuf);
                mediaBuf[length - 4] = '.';
                mediaBuf[length - 3] = '.';
                mediaBuf[length - 2] = '.';
                mediaBuf[length - 1] = '\0';
                display.getTextBounds(mediaBuf, 0, 0, &boundsX, &boundsY, &textWidth, &textHeight);
            }
            drawRightAlignedText(display, mediaBuf, 164, 150, ersa::hal::FontFace::MiSansRegular8);
            drawMusicNote(display, 174, 136);
        }
    }

    drawRightAlignedText(display, daysLower[dow], 184, 168, ersa::hal::FontFace::MiSansRegular8);
    drawRightAlignedText(display, dateBuf, 184, 184, ersa::hal::FontFace::MiSansRegular8);

    // Keep status clear of the full-width date on the bottom row.
    constexpr int16_t leftX = 18;
    if (NetSync::isSyncing()) {
        display.setFont(ersa::hal::FontFace::MiSansRegular8);
        display.setCursor(leftX, 134);
        display.print(ersa::strings::MSG_SYNCING);
    } else if (ersa::services::PowerManager::instance().isBatteryConnected() &&
               ersa::services::PowerManager::instance().getBatteryPercent() <= 20) {
        display.setFont(ersa::hal::FontFace::MiSansRegular8);
        display.setCursor(leftX, 134);
        display.print(ersa::strings::MSG_LOW_BATT);
    }

    display.setTextColor(1);    // 1 = GxEPD_WHITE

    // 2. Generate words for time
    const char* l1 = "";
    const char* l2 = "";
    const char* l3 = "";
    timeToWords(time.hour(), time.minute(), l1, l2, l3);

    if (l3[0] != '\0') {
        // 3 lines layout
        display.setFont(ersa::hal::FontFace::MiSansBold17);
        display.setCursor(leftX, 48);
        display.print(l1);

        display.setFont(ersa::hal::FontFace::MiSansLight17);
        display.setCursor(leftX, 80);
        display.print(l2);

        display.setCursor(leftX, 112);
        display.print(l3);
    } else {
        // 2 lines layout: elegant vertical centering
        display.setFont(ersa::hal::FontFace::MiSansBold17);
        display.setCursor(leftX, 58);
        display.print(l1);

        display.setFont(ersa::hal::FontFace::MiSansLight17);
        display.setCursor(leftX, 94);
        display.print(l2);
    }
}

} // namespace WatchfaceClock
