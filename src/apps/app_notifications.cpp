#include "app_notifications.h"
#include "ersa/services/bluetooth_manager.h"
#include "ersa/app/application_manager.h"
#include "ui/text_layout.h"
#include "core/debug_log.h"
#include <Arduino.h>

namespace AppNotifications {

namespace {
size_t currentIndex = 0;

void printWrapped(ersa::hal::IDisplay& display, const char* text, int16_t x, int16_t startY, int16_t lineSpacing, int maxLines, int maxCharsPerLine) {
    if (!text || text[0] == '\0') return;

    int line = 0;
    const char* p = text;

    while (*p && line < maxLines) {
        // Find line break or word wrap point
        int len = 0;
        int lastSpace = -1;
        while (p[len] && len < maxCharsPerLine) {
            if (p[len] == ' ') lastSpace = len;
            if (p[len] == '\n') {
                lastSpace = len;
                break;
            }
            len++;
        }

        int printLen = len;
        if (p[len] != '\0' && p[len] != '\n' && lastSpace > 0) {
            printLen = lastSpace;
        }

        char lineBuf[32];
        int copyLen = (printLen < int(sizeof(lineBuf) - 1)) ? printLen : (int(sizeof(lineBuf) - 1));
        memcpy(lineBuf, p, copyLen);
        lineBuf[copyLen] = '\0';

        WatchText::line(display, lineBuf, x, startY + line * lineSpacing, 164);
        line++;

        p += printLen;
        while (*p == ' ' || *p == '\n') p++; // skip delimiter
    }
}
} // namespace

void begin() {
    currentIndex = 0;
}

bool onButton(Buttons::Event event) {
    auto& bleMgr = ersa::services::BluetoothManager::instance();
    const size_t total = bleMgr.getNotificationCount();

    if (event == Buttons::Event::Next) {
        if (total > 1) {
            currentIndex = (currentIndex + 1) % total;
            DebugLog::log("NOTIF: Next notification -> index %u/%u", unsigned(currentIndex + 1), unsigned(total));
            return true;
        }
    } else if (event == Buttons::Event::Action || event == Buttons::Event::ActionAlt) {
        if (total == 0) {
            ersa::app::ApplicationManager::instance().switchTo("watchface_clock");
            return true;
        }
        if (currentIndex >= total) currentIndex = 0;
        const uint32_t uid = bleMgr.getNotification(currentIndex).uid;
        if (bleMgr.dismissNotification(currentIndex)) {
            DebugLog::log("NOTIF: Dismissed local uid=%lu", (unsigned long)uid);
            if (currentIndex >= bleMgr.getNotificationCount() && currentIndex > 0) --currentIndex;
        }
        return true;
    } else if (event == Buttons::Event::ActionLong || event == Buttons::Event::Home) {
        ersa::app::ApplicationManager::instance().switchTo("watchface_clock");
        return true;
    }

    return false;
}

void render(ersa::hal::IDisplay& display, bool full) {
    (void)full;
    auto& bleMgr = ersa::services::BluetoothManager::instance();
    const size_t total = bleMgr.getNotificationCount();

    display.fillScreen(0);   // Solid black
    display.setTextColor(1); // White

    if (total == 0) {
        display.setFont(ersa::hal::FontFace::MiSansBold10);
        display.setCursor(18, 24);
        display.print("notifications");
        display.setFont(ersa::hal::FontFace::MiSansRegular8);
        WatchText::line(display, "no new alerts", 18, 89, 164);
        WatchText::line(display, "from your iphone", 18, 114, 164);
        display.drawFastHLine(18, 135, 164, 1);
        WatchText::line(display, "b2: back", 18, 186, 164);
        return;
    }

    if (currentIndex >= total) {
        currentIndex = 0;
    }

    const auto& notif = bleMgr.getNotification(currentIndex);

    // 1. Header
    display.setFont(ersa::hal::FontFace::MiSansBold10);
    display.setCursor(18, 24);
    display.print("notifications");

    // Page indicator (e.g. "1/3")
    if (total > 1) {
        char countBuf[16];
        snprintf(countBuf, sizeof(countBuf), "%u/%u", unsigned(currentIndex + 1), unsigned(total));
        display.setFont(ersa::hal::FontFace::MiSansRegular8);
        display.setCursor(160, 24);
        display.print(countBuf);
    }

    display.setFont(ersa::hal::FontFace::MiSansRegular8);
    const char* source = (notif.app[0] && strcmp(notif.app, notif.title) != 0)
        ? notif.app : "from iphone";
    WatchText::line(display, source, 18, 47, 164);

    display.setFont(ersa::hal::FontFace::MiSansBold10);
    WatchText::line(display, notif.title[0] ? notif.title : "notification", 18, 82, 164);
    display.drawFastHLine(18, 96, 164, 1);

    // Message Body wrapped
    display.setFont(ersa::hal::FontFace::MiSansRegular8);
    printWrapped(display, notif.message, 18, 115, 17, 3, 22);

    display.setFont(ersa::hal::FontFace::MiSansRegular8);
    if (total > 1) {
        WatchText::line(display, "b1: next", 18, 168, 164);
    } else {
        WatchText::line(display, "hold b1: back", 18, 168, 164);
    }
    WatchText::line(display, notif.canDismissRemotely ? "b2: dismiss on phone" : "b2: dismiss", 18, 186, 164);
}

} // namespace AppNotifications
