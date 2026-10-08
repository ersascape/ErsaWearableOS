#include "watchfaces/app_watchface.h"
#include "watchfaces/watchface_clock.h"
#include "ersa/app/application_manager.h"
#include "ersa/services/bluetooth_manager.h"
#include "core/watch_clock.h"
#include "core/debug_log.h"

#if defined(ARDUINO)
#endif

namespace ersa {
namespace watchface {

AppWatchface& AppWatchface::instance() {
    static AppWatchface s_watchface;
    return s_watchface;
}

AppWatchface::AppWatchface() = default;

void AppWatchface::onEnter() {
    shownMinute_ = UINT32_MAX;
    DebugLog::log("APP: Watchface onEnter");
}

void AppWatchface::onExit() {
    DebugLog::log("APP: Watchface onExit");
}

void AppWatchface::onEvent(const events::Event& event) {
    if (event.type == events::EventType::ButtonClicked) {
        if (event.button.button == events::ButtonId::Button1) {
            DebugLog::log("NAV: watchface top-button click -> app drawer");
            app::ApplicationManager::instance().switchTo("app_drawer");
        } else if (event.button.button == events::ButtonId::Button2) {
            // Keep a tap on the clock navigation-only. Full Wi-Fi/CalDAV sync
            // blocks the UI and can fail under BLE heap pressure; launch it
            // deliberately from Agenda or Tasks instead.
            DebugLog::log("NAV: watchface action-button click -> app drawer");
            app::ApplicationManager::instance().switchTo("app_drawer");
        }
    } else if (event.type == events::EventType::ButtonLongPressed) {
        if (event.button.button == events::ButtonId::Button1) {
            // Hold B1: Quick dial most recent contact
            auto& bleMgr = services::BluetoothManager::instance();
            if (bleMgr.getRecentCallCount() > 0) {
                DebugLog::log("WATCHFACE: Hold B1 -> Quick dial recent %s", bleMgr.getRecentCall(0).name);
                bleMgr.dialRecent(0);
                app::ApplicationManager::instance().switchTo("app_call");
                app::ApplicationManager::instance().markDirty(false);
            }
        } else if (event.button.button == events::ButtonId::Button2) {
            app::ApplicationManager::instance().switchTo("app_drawer");
        }
    }
}

void AppWatchface::render(hal::IDisplay& display, bool fullRefresh) {
#if defined(ARDUINO)
    DateTime time = WatchClock::now();
    WatchfaceClock::render(display, time, fullRefresh);
    shownMinute_ = time.unixtime() / 60;
    shownDay_ = time.day();
    shownRtcHealthy_ = WatchClock::healthy();
#else
    (void)display; (void)fullRefresh;
#endif
}

} // namespace watchface
} // namespace ersa
