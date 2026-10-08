#include "app_status.h"
#include "core/watch_clock.h"
#include "core/debug_log.h"
#include "core/battery.h"
#include "core/net_sync.h"
#include "ersa/services/bluetooth_manager.h"
#include "ersa/services/ota_service.h"
#include "ersa/board/board.h"
#include "ui/text_layout.h"
#include "ersa/config/ui_strings.h"

namespace AppStatus {

static void formatUptime(char* output, size_t capacity, uint32_t seconds) {
    const uint32_t days = seconds / 86400U;
    const uint32_t hours = (seconds / 3600U) % 24U;
    const uint32_t minutes = (seconds / 60U) % 60U;
    if (days) snprintf(output, capacity, "%lud %luh", (unsigned long)days, (unsigned long)hours);
    else if (hours) snprintf(output, capacity, "%luh %02lum", (unsigned long)hours, (unsigned long)minutes);
    else snprintf(output, capacity, "%lum %02lus", (unsigned long)minutes,
                  (unsigned long)(seconds % 60U));
}

void render(ersa::hal::IDisplay& display) {
    display.fillScreen(0);   // Solid black
    display.setTextColor(1); // White

    // Clean lowercase header
    display.setFont(ersa::hal::FontFace::MiSansBold10);
    display.setCursor(18, 24);
    display.print(ersa::strings::APP_TITLE_STATUS);

    display.setFont(ersa::hal::FontFace::MiSansRegular8);

    constexpr int16_t leftX = 18;
    constexpr int16_t valX = 86;
    constexpr int16_t startY = 45;
    constexpr int16_t rowHeight = 14;

    // 1. RTC
    display.setCursor(leftX, startY);
    display.print("rtc");
    display.setCursor(valX, startY);
    display.print(WatchClock::healthy() ? ersa::strings::MSG_ONLINE : ersa::strings::MSG_OFFLINE);

    // 2. Battery
    display.setCursor(leftX, startY + rowHeight);
    display.print("battery");
    display.setCursor(valX, startY + rowHeight);
    if (Battery::isConnected()) {
        char battBuf[20];
        snprintf(battBuf, sizeof(battBuf), "%u%% (%u mV)",
                 Battery::percentage(), Battery::millivolts());
        display.print(battBuf);
    } else {
        display.print(ersa::strings::MSG_USB_POWER);
    }

    // 3. Current boot uptime
    char uptime[20];
    formatUptime(uptime, sizeof(uptime), ersa::board::Board::current().getUptimeMs() / 1000U);
    display.setCursor(leftX, startY + rowHeight * 2);
    display.print("uptime");
    display.setCursor(valX, startY + rowHeight * 2);
    display.print(uptime);

    // 4. Reset
    display.setCursor(leftX, startY + rowHeight * 3);
    display.print("reset");
    display.setCursor(valX, startY + rowHeight * 3);
    display.print(DebugLog::resetReasonName());

    auto& ble = ersa::services::BluetoothManager::instance();
    display.setCursor(leftX, startY + rowHeight * 4);
    display.print("ble");
    WatchText::line(display, ble.isConnected() ? "connected" :
                    ble.isAdvertising() ? "advertising" : "starting", valX, startY + rowHeight * 4, 98);
    display.setCursor(leftX, startY + rowHeight * 5);
    display.print("apple");
    WatchText::line(display, ble.notificationsReady() && ble.mediaReady() ? "ready" :
                    ble.notificationsReady() ? "alerts ready" :
                    ble.mediaReady() ? "music ready" : "waiting", valX, startY + rowHeight * 5, 98);

    // 7. Net Sync
    display.setCursor(leftX, startY + rowHeight * 6);
    display.print("sync");
    display.setCursor(valX, startY + rowHeight * 6);
    WatchText::line(display, NetSync::lastStatus(), valX, startY + rowHeight * 6, 98);

    // The app descriptor is compiled into this exact image and distinguishes
    // release firmware from local test builds.
    display.setCursor(leftX, startY + rowHeight * 7);
    display.print("version");
    WatchText::line(display, ersa::services::OtaService::instance().runningVersion(),
                    valX, startY + rowHeight * 7, 98);

    // Clean footer
    WatchText::line(display, ble.isConnected() ? "ble connected" : "b1: reconnect ble", leftX, 168, 166);
    WatchText::line(display, "b2: sync / hold b1: back", leftX, 186, 166);
}

bool onButton(Buttons::Event event) {
    if (event == Buttons::Event::Next) {
        ersa::services::BluetoothManager::instance().restartAdvertising();
        return true;
    }
    if (event == Buttons::Event::Action || event == Buttons::Event::ActionLong) {
        NetSync::syncNtp();
        return true;
    }
    return false;
}

} // namespace AppStatus
