#include "apps/apps_registry.h"
#include "watchfaces/app_watchface.h"
#include "apps/app_drawer.h"
#include "apps/app_calendar.h"
#include "apps/app_agenda.h"
#include "apps/app_todo.h"
#include "apps/app_portal.h"
#include "apps/app_status.h"
#include "apps/app_now_playing.h"
#include "apps/app_call.h"
#include "apps/app_notifications.h"
#include "ersa/services/ota_service.h"
#include "ersa/board/board.h"
#include "core/buttons.h"
#include "core/watch_clock.h"
#include "core/debug_log.h"


namespace ersa {
namespace app {

namespace {

Buttons::Event toLegacyButtonEvent(const events::Event& event) {
    if (event.button.button == events::ButtonId::Button1) {
        if (event.type == events::EventType::ButtonClicked) return Buttons::Event::Next;
        if (event.type == events::EventType::ButtonDoubleClicked) return Buttons::Event::Previous;
        if (event.type == events::EventType::ButtonLongPressed) return Buttons::Event::Home;
    } else if (event.button.button == events::ButtonId::Button2) {
        if (event.type == events::EventType::ButtonClicked) return Buttons::Event::Action;
        if (event.type == events::EventType::ButtonDoubleClicked) return Buttons::Event::ActionAlt;
        if (event.type == events::EventType::ButtonLongPressed) return Buttons::Event::ActionLong;
    }
    return Buttons::Event::None;
}

// 1. AppDrawer Application
class DrawerApp : public Application {
public:
    const char* getId() const override { return "app_drawer"; }
    const char* getTitle() const override { return "App Drawer"; }
    Rect getPartialBounds() const override { return Rect{0, 24, 200, 152}; }

    void onEvent(const events::Event& event) override {
        const auto legacy = toLegacyButtonEvent(event);
        if (legacy == Buttons::Event::Home) {
            ApplicationManager::instance().switchTo("watchface_clock");
        } else if (legacy == Buttons::Event::Next) {
            AppDrawer::next();
            ApplicationManager::instance().markDirty(false);
        } else if (legacy == Buttons::Event::Previous) {
            AppDrawer::previous();
            ApplicationManager::instance().markDirty(false);
        } else if (legacy == Buttons::Event::Action) {
            const auto item = AppDrawer::selected();
            switch (item) {
                case AppDrawer::Item::Clock:
                    ApplicationManager::instance().switchTo("watchface_clock");
                    break;
                case AppDrawer::Item::Calendar:
                    ApplicationManager::instance().switchTo("app_calendar");
                    break;
                case AppDrawer::Item::Agenda:
                    ApplicationManager::instance().switchTo("app_agenda");
                    break;
                case AppDrawer::Item::Todo:
                    ApplicationManager::instance().switchTo("app_todo");
                    break;
                case AppDrawer::Item::NowPlaying:
                    ApplicationManager::instance().switchTo("app_media");
                    break;
                case AppDrawer::Item::Calls:
                    ApplicationManager::instance().switchTo("app_call");
                    break;
                case AppDrawer::Item::Notifications:
                    ApplicationManager::instance().switchTo("app_notifications");
                    break;
                case AppDrawer::Item::Hotspot:
                    ApplicationManager::instance().switchTo("app_portal");
                    break;
                case AppDrawer::Item::Status:
                    ApplicationManager::instance().switchTo("app_status");
                    break;
                case AppDrawer::Item::Updater:
                    ApplicationManager::instance().switchTo("app_updater");
                    break;
                default:
                    break;
            }
        }
    }

    void render(hal::IDisplay& display, bool fullRefresh) override {
#if defined(ARDUINO)
        AppDrawer::render(display, fullRefresh);
#else
        (void)display; (void)fullRefresh;
#endif
    }
};

class UpdaterApp : public Application {
public:
    const char* getId() const override { return "app_updater"; }
    const char* getTitle() const override { return "Updater"; }
    Rect getPartialBounds() const override { return Rect{0, 24, 200, 152}; }

    void onEnter() override { lastState_ = service().updateState(); }

    void onEvent(const events::Event& event) override {
        const auto legacy = toLegacyButtonEvent(event);
        if (legacy == Buttons::Event::Home) {
            service().resumeAfterCheck();
            ApplicationManager::instance().switchTo("app_drawer");
        } else if (legacy == Buttons::Event::Action) {
            if (service().updateState() == ersa::services::OtaService::UpdateState::Available)
                service().installUpdate();
            else
                service().checkForUpdate();
            ApplicationManager::instance().markDirty(false);
        } else if (legacy == Buttons::Event::ActionAlt && service().otherSlotBootable()) {
            if (service().selectOtherSlot()) {
                board::Board::current().delayMs(500);
                board::Board::current().restart();
            }
        }
    }

    void tick() override {
        const auto state = service().updateState();
        if (state != lastState_) {
            lastState_ = state;
            ApplicationManager::instance().markDirty(false);
        }
    }

    void render(hal::IDisplay& display, bool fullRefresh) override {
        (void)fullRefresh;
#if defined(ARDUINO)
        auto& gfx = display;
        gfx.fillScreen(0);
        gfx.setTextColor(1);
        gfx.setFont(ersa::hal::FontFace::MiSansBold10);
        gfx.setCursor(16, 24);
        gfx.print("system update");
        gfx.setFont(ersa::hal::FontFace::MiSansRegular8);
        gfx.setCursor(16, 52);
        gfx.print("Running "); gfx.print(service().runningSlot());
        gfx.setCursor(16, 72);
        gfx.print("Image "); gfx.print(service().runningImageState());
        gfx.setCursor(16, 91);
        gfx.print("Other "); gfx.print(service().otherSlot());
        gfx.print(" "); gfx.print(service().otherImageState());
        gfx.setCursor(16, 105);
        gfx.print(service().updateMessage());
        if (service().updateVersion()[0]) {
            gfx.setCursor(16, 126);
            gfx.print("Release "); gfx.print(service().updateVersion());
        }
        gfx.setFont(ersa::hal::FontFace::MiSansRegular8);
        gfx.setCursor(16, 170);
        gfx.print(service().updateState() == ersa::services::OtaService::UpdateState::Available
                      ? "B2 install   hold B1 drawer" : "B2 check   hold B1 drawer");
        gfx.setCursor(16, 190);
        gfx.print(service().otherSlotBootable() ? "B2x2 boot other slot" : "other slot not bootable");
#else
        (void)display;
#endif
    }

private:
    static ersa::services::OtaService& service() { return ersa::services::OtaService::instance(); }
    ersa::services::OtaService::UpdateState lastState_{ersa::services::OtaService::UpdateState::Idle};
};

// 2. AppCalendar Application
class CalendarApp : public Application {
public:
    const char* getId() const override { return "app_calendar"; }
    const char* getTitle() const override { return "Calendar"; }

    void onEnter() override {
        AppCalendar::resetToCurrentMonth(WatchClock::now());
    }

    void onEvent(const events::Event& event) override {
        const auto legacy = toLegacyButtonEvent(event);
        if (legacy == Buttons::Event::Home) {
            ApplicationManager::instance().switchTo("app_drawer");
            return;
        }
        if (AppCalendar::onButton(legacy, WatchClock::now())) {
            ApplicationManager::instance().markDirty(false);
        }
    }

    void render(hal::IDisplay& display, bool fullRefresh) override {
        (void)fullRefresh;
#if defined(ARDUINO)
        AppCalendar::render(display, WatchClock::now());
#else
        (void)display;
#endif
    }
};

// 3. AppAgenda Application
class AgendaApp : public Application {
public:
    const char* getId() const override { return "app_agenda"; }
    const char* getTitle() const override { return "Agenda"; }

    void onEvent(const events::Event& event) override {
        const auto legacy = toLegacyButtonEvent(event);
        if (legacy == Buttons::Event::Home) {
            ApplicationManager::instance().switchTo("app_drawer");
            return;
        }
        if (AppAgenda::onButton(legacy)) {
            ApplicationManager::instance().markDirty(false);
        }
    }

    void render(hal::IDisplay& display, bool fullRefresh) override {
#if defined(ARDUINO)
        AppAgenda::render(display, WatchClock::now(), fullRefresh);
#else
        (void)display; (void)fullRefresh;
#endif
    }
};

// 4. AppTodo Application
class TodoApp : public Application {
public:
    const char* getId() const override { return "app_todo"; }
    const char* getTitle() const override { return "Todo"; }

    void onEvent(const events::Event& event) override {
        const auto legacy = toLegacyButtonEvent(event);
        if (legacy == Buttons::Event::Home) {
            ApplicationManager::instance().switchTo("app_drawer");
            return;
        }
        if (AppTodo::onButton(legacy)) {
            ApplicationManager::instance().markDirty(false);
        }
    }

    void render(hal::IDisplay& display, bool fullRefresh) override {
#if defined(ARDUINO)
        AppTodo::render(display, fullRefresh);
#else
        (void)display; (void)fullRefresh;
#endif
    }
};

// 5. AppPortal Application
class PortalApp : public Application {
public:
    const char* getId() const override { return "app_portal"; }
    const char* getTitle() const override { return "Hotspot Portal"; }

    void onStop() override {
        AppPortal::stop();
    }

    void onEvent(const events::Event& event) override {
        const auto legacy = toLegacyButtonEvent(event);
        if (legacy == Buttons::Event::Home) {
            ApplicationManager::instance().switchTo("app_drawer");
            return;
        }
        if (AppPortal::onButton(legacy)) {
            ApplicationManager::instance().markDirty(false);
        }
    }

    void render(hal::IDisplay& display, bool fullRefresh) override {
        (void)fullRefresh;
#if defined(ARDUINO)
        AppPortal::render(display);
#else
        (void)display;
#endif
    }

    void tick() override {
        AppPortal::tick();
    }
};

// 6. AppStatus Application
class StatusApp : public Application {
public:
    const char* getId() const override { return "app_status"; }
    const char* getTitle() const override { return "Status"; }

    void onEvent(const events::Event& event) override {
        const auto legacy = toLegacyButtonEvent(event);
        if (legacy == Buttons::Event::Home) {
            ApplicationManager::instance().switchTo("app_drawer");
            return;
        }
        if (AppStatus::onButton(legacy)) {
            ApplicationManager::instance().markDirty(false);
        }
    }

    void render(hal::IDisplay& display, bool fullRefresh) override {
        (void)fullRefresh;
#if defined(ARDUINO)
        AppStatus::render(display);
#else
        (void)display;
#endif
    }
};

// 7. AppNowPlaying Application
class MediaApp : public Application {
public:
    const char* getId() const override { return "app_media"; }
    const char* getTitle() const override { return "Now Playing"; }

    void onEvent(const events::Event& event) override {
        const auto legacy = toLegacyButtonEvent(event);
        if (AppNowPlaying::onButton(legacy)) {
            ApplicationManager::instance().markDirty(false);
        }
    }

    void render(hal::IDisplay& display, bool fullRefresh) override {
        (void)fullRefresh;
#if defined(ARDUINO)
        AppNowPlaying::render(display);
#else
        (void)display;
#endif
    }
};

// 8. AppCall Application
class CallApp : public Application {
public:
    const char* getId() const override { return "app_call"; }
    const char* getTitle() const override { return "Calls"; }

    void onEvent(const events::Event& event) override {
        const auto legacy = toLegacyButtonEvent(event);
        if (AppCall::onButton(legacy)) {
            ApplicationManager::instance().markDirty(false);
        }
    }

    void render(hal::IDisplay& display, bool fullRefresh) override {
        (void)fullRefresh;
#if defined(ARDUINO)
        AppCall::render(display);
#else
        (void)display;
#endif
    }
};

// 9. AppNotifications Application
class NotificationApp : public Application {
public:
    const char* getId() const override { return "app_notifications"; }
    const char* getTitle() const override { return "Notifications"; }

    void onEvent(const events::Event& event) override {
        const auto legacy = toLegacyButtonEvent(event);
        if (AppNotifications::onButton(legacy)) {
            ApplicationManager::instance().markDirty(false);
        }
    }

    void render(hal::IDisplay& display, bool fullRefresh) override {
        (void)fullRefresh;
#if defined(ARDUINO)
        AppNotifications::render(display);
#else
        (void)display;
#endif
    }
};

static DrawerApp s_drawerApp;
static CalendarApp s_calendarApp;
static AgendaApp s_agendaApp;
static TodoApp s_todoApp;
static PortalApp s_portalApp;
static StatusApp s_statusApp;
static MediaApp s_mediaApp;
static CallApp s_callApp;
static NotificationApp s_notifApp;
static UpdaterApp s_updaterApp;

} // namespace

void registerAllApps(ApplicationManager& manager) {
    AppDrawer::begin();
    AppCalendar::begin();
    AppAgenda::begin();
    AppTodo::begin();
    AppPortal::begin();
    AppNowPlaying::begin();
    AppCall::begin();
    AppNotifications::begin();

    manager.registerApp(&watchface::AppWatchface::instance());
    manager.registerApp(&s_drawerApp);
    manager.registerApp(&s_calendarApp);
    manager.registerApp(&s_agendaApp);
    manager.registerApp(&s_todoApp);
    manager.registerApp(&s_portalApp);
    manager.registerApp(&s_statusApp);
    manager.registerApp(&s_mediaApp);
    manager.registerApp(&s_callApp);
    manager.registerApp(&s_notifApp);
    manager.registerApp(&s_updaterApp);
}

} // namespace app
} // namespace ersa
