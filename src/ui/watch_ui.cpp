#include "ui/watch_ui.h"
#include "ersa/board/board.h"
#include "ersa/events/event_bus.h"
#include "ersa/app/application_manager.h"
#include "ersa/services/time_service.h"
#include "ersa/services/power_manager.h"
#include "ersa/services/network_manager.h"
#include "ersa/services/display_manager.h"
#include "ersa/services/bluetooth_manager.h"
#include "ersa/services/session_stats.h"
#include "ersa/runtime/scheduler.h"
#include "apps/apps_registry.h"
#include "apps/app_portal.h"
#include "core/watch_clock.h"
#include "core/debug_log.h"
#include "core/net_sync.h"

namespace {

ersa::board::Board& board = ersa::board::Board::current();
ersa::events::EventBus& eventBus = ersa::events::EventBus::instance();
ersa::app::ApplicationManager& appManager = ersa::app::ApplicationManager::instance();

ersa::services::TimeService timeService(board.getRtc(), eventBus);
ersa::services::PowerManager powerManager(board.getBattery(), eventBus);
ersa::services::DisplayManager displayManager(board.getDisplay());
ersa::services::NetworkManager networkManager(eventBus, &board.getWifi());
ersa::services::BluetoothManager bluetoothManager(board.getBluetooth(), board.getCompanionSource(), eventBus);

uint32_t shownMinute = UINT32_MAX;
uint8_t shownDay = 0;
bool shownRtcHealthy = false;
uint32_t lastFrameEnd = 0;
uint32_t lastActivityMs = 0;
uint32_t lastUserInputMs = 0;
bool firstFrame = true;
bool watchfaceRectPending = false;
ersa::Rect watchfaceRect{0, 0, 0, 0};
bool watchfaceMediaPending = false;
uint32_t watchfaceMediaChangedAt = 0;
char renderedWatchfaceMediaTitle[32] = {0};
uint32_t bothButtonsPressedAt = 0;
bool bothButtonResetTriggered = false;
constexpr uint32_t BOTH_BUTTON_RESET_HOLD_MS = 3000;

auto& powerHal = board.getPowerManagement();
bool automaticSleepReady = false;
bool bootTimeFallbackConsidered = false;

void queueWatchfaceMediaRefresh() {
    watchfaceMediaPending = true;
    watchfaceMediaChangedAt = board.getUptimeMs();
}

void invalidateWatchface(const ersa::Rect& rect) {
    if (watchfaceRectPending) {
        const int16_t x1 = (rect.x < watchfaceRect.x) ? rect.x : watchfaceRect.x;
        const int16_t y1 = (rect.y < watchfaceRect.y) ? rect.y : watchfaceRect.y;
        const int16_t x2a = watchfaceRect.x + watchfaceRect.w;
        const int16_t x2b = rect.x + rect.w;
        const int16_t y2a = watchfaceRect.y + watchfaceRect.h;
        const int16_t y2b = rect.y + rect.h;
        watchfaceRect = ersa::Rect{x1, y1, int16_t((x2a > x2b ? x2a : x2b) - x1),
                                   int16_t((y2a > y2b ? y2a : y2b) - y1)};
    } else {
        watchfaceRect = rect;
        watchfaceRectPending = true;
    }
    appManager.markDirty(false);
}

void renderCurrentApp() {
    const uint32_t started = board.getUptimeMs();
    const DateTime time = WatchClock::now();
    auto* activeApp = appManager.getActiveApp();
    if (!activeApp) return;

    auto& display = board.getDisplay();

    // Use a full waveform only for initial cleanup or a day transition. The
    // display manager owns periodic ghost-clearing cadence for partial frames.
    const bool appSwitched = appManager.isAppSwitched();
    const bool dayChanged = (shownDay != 0 && time.day() != shownDay);
    const bool hardwareFull = firstFrame || dayChanged;

    DebugLog::log("EPD begin app=%s hwFull=%d time=%02u:%02u:%02u",
                  activeApp->getId(), hardwareFull,
                  unsigned(time.hour()), unsigned(time.minute()), unsigned(time.second()));

    display.fillScreen(0);
    display.setTextColor(1);
    display.setTextWrap(false);
    {
        ersa::hal::PerformanceScope frequency(powerHal, ersa::hal::PerformanceProfile::Compute, "app-render");
        activeApp->render(display, true);
    }
    if (strcmp(activeApp->getId(), "watchface_clock") == 0) {
        const auto callState = bluetoothManager.getCallState();
        const char* title = (callState == ersa::services::CallState::Incoming ||
                             callState == ersa::services::CallState::Active)
                                ? "" : bluetoothManager.getMediaTitle();
        if (!title || strcmp(title, "No Media") == 0) title = "";
        strncpy(renderedWatchfaceMediaTitle, title, sizeof(renderedWatchfaceMediaTitle) - 1);
        renderedWatchfaceMediaTitle[sizeof(renderedWatchfaceMediaTitle) - 1] = '\0';
    }

    if (hardwareFull) {
        displayManager.refreshRect(ersa::Rect{0, 0, display.width(), display.height()}, true, board.getUptimeMs());
        firstFrame = false;
    } else if (appSwitched) {
        // App transitions replace the whole page. Refresh the whole panel with
        // the partial waveform so stale drawer/footer pixels cannot survive.
        displayManager.refreshRect(ersa::Rect{0, 0, display.width(), display.height()}, false, board.getUptimeMs());
    } else {
        // Let the display HAL honor the app's invalidated area. The old path
        // sent a 200x200 partial update for every redraw, flashing the entire
        // panel even for routine watchface changes.
        if (strcmp(activeApp->getId(), "watchface_clock") == 0 && watchfaceRectPending) {
            displayManager.refreshRect(watchfaceRect, false, board.getUptimeMs());
        } else {
            displayManager.refreshRect(activeApp->getPartialBounds(), false, board.getUptimeMs());
        }
    }
    watchfaceRectPending = false;
    appManager.clearAppSwitched();

    lastActivityMs = board.getUptimeMs();
    shownMinute = time.unixtime() / 60;
    shownDay = time.day();
    shownRtcHealthy = WatchClock::healthy();
    lastFrameEnd = board.getUptimeMs();

    DebugLog::log("EPD end duration=%lu ms BUSY=%d (partialFrames=%u)",
                  (unsigned long)(lastFrameEnd - started), display.isBusy(),
                  unsigned(displayManager.getPartialFrameCount()));
}

} // namespace

void WatchUi::begin() {
    DebugLog::log("UI: initializing %s board", board.getDeviceInfo().name);
    uint8_t initOk = 0;
    uint8_t initDegraded = 0;
    uint8_t initFailed = 0;
    auto checkInit = [&](const char* name, bool ok, bool degraded = false) {
        if (degraded) {
            ++initDegraded;
            DebugLog::log("BOOT CHECK %s=degraded", name);
        } else if (!ok) {
            ++initFailed;
            DebugLog::log("BOOT CHECK %s=failed", name);
        } else {
            ++initOk;
            DebugLog::log("BOOT CHECK %s=ok", name);
        }
    };

    const auto inputInit = board.init();
    checkInit("buttons", inputInit.isOk());
    ersa::services::NetworkManager::setInstance(&networkManager);
    const auto networkInit = networkManager.init();
    checkInit("network", networkInit.isOk());

    automaticSleepReady = powerHal.initializeWakeSources(board.getPins());
    if (automaticSleepReady)
        DebugLog::log("PWR: light sleep armed after 30s inactivity; wake sources BLE, minute timer, buttons");
    else
        DebugLog::log("PWR: cannot initialize automatic sleep wake sources");
    bluetoothManager.setWakeCallback([](void*) { board.getPowerManagement().notifyWake(); }, nullptr);

    const auto timeInit = timeService.init();
    checkInit("rtc", timeInit.isOk() && timeService.isRtcHealthy(),
              timeInit.isOk() && !timeService.isRtcHealthy());
    ersa::services::TimeService::setInstance(&timeService);

    const auto powerInit = powerManager.init();
    checkInit("battery", powerInit.isOk());
    ersa::services::PowerManager::setInstance(&powerManager);

    const auto displayInit = displayManager.init();
    checkInit("display", displayInit.isOk());
    ersa::services::DisplayManager::setInstance(&displayManager);

    const auto bluetoothInit = bluetoothManager.init();
    checkInit("bluetooth", bluetoothInit.isOk(), !bluetoothInit.isOk());
    ersa::services::BluetoothManager::setInstance(&bluetoothManager);
    ersa::services::SessionStats::begin();
    DebugLog::log("SESSION: last session uptime approximately %lu seconds",
                  static_cast<unsigned long>(ersa::services::SessionStats::previousSessionUptimeSeconds()));

    // Subscribe ApplicationManager to EventBus for decoupled event routing
    eventBus.subscribe(ersa::events::EventType::None, [](const ersa::events::Event& evt, void* user) {
        auto* mgr = static_cast<ersa::app::ApplicationManager*>(user);
        if (evt.type == ersa::events::EventType::ButtonClicked ||
            evt.type == ersa::events::EventType::ButtonDoubleClicked ||
            evt.type == ersa::events::EventType::ButtonLongPressed ||
            evt.type == ersa::events::EventType::BleConnected ||
            evt.type == ersa::events::EventType::BleDisconnected ||
            evt.type == ersa::events::EventType::CompanionConnected ||
            evt.type == ersa::events::EventType::CompanionDisconnected ||
            evt.type == ersa::events::EventType::CallIncoming ||
            evt.type == ersa::events::EventType::CallAccepted ||
            evt.type == ersa::events::EventType::CallRejected ||
            evt.type == ersa::events::EventType::CallEnded ||
            evt.type == ersa::events::EventType::MediaTrackChanged ||
            evt.type == ersa::events::EventType::MediaStateChanged ||
            evt.type == ersa::events::EventType::NotificationReceived ||
            evt.type == ersa::events::EventType::NotificationRemoved ||
            evt.type == ersa::events::EventType::NotificationsCleared ||
            evt.type == ersa::events::EventType::TimeSync) {
            const uint32_t now = board.getUptimeMs();
            lastActivityMs = now;
            displayManager.noteActivity(now);
            powerManager.noteActivity(now);
            powerHal.allowAutomaticSleep(false);
        }
        if (mgr) {
            mgr->handleEvent(evt);
            if (evt.type == ersa::events::EventType::ButtonClicked ||
                evt.type == ersa::events::EventType::ButtonDoubleClicked ||
                evt.type == ersa::events::EventType::ButtonLongPressed) {
                mgr->markDirty(false);
            }
        }
    }, &appManager);

    // Auto-switch to call screen on incoming call
    eventBus.subscribe(ersa::events::EventType::CallIncoming, [](const ersa::events::Event&, void* user) {
        auto* mgr = static_cast<ersa::app::ApplicationManager*>(user);
        if (mgr) {
            mgr->switchTo("app_call");
            mgr->markDirty(false);
        }
    }, &appManager);

    // The full Now Playing app reacts to all AMS updates. The watchface only
    // renders the track title, so ignore artist and playback-only updates.
    eventBus.subscribe(ersa::events::EventType::MediaTrackChanged, [](const ersa::events::Event& evt, void* user) {
        auto* mgr = static_cast<ersa::app::ApplicationManager*>(user);
        if (mgr && mgr->getActiveApp()) {
            const char* id = mgr->getActiveApp()->getId();
            if (strcmp(id, "app_media") == 0) {
                mgr->markDirty(false);
            } else if (strcmp(id, "watchface_clock") == 0) {
                const auto callState = bluetoothManager.getCallState();
                const char* title = (evt.media.title[0] == '\0' || strcmp(evt.media.title, "No Media") == 0)
                                        ? "" : evt.media.title;
                if (callState != ersa::services::CallState::Incoming &&
                    callState != ersa::services::CallState::Active && title &&
                    strcmp(title, renderedWatchfaceMediaTitle) != 0) {
                    queueWatchfaceMediaRefresh();
                }
            }
        }
    }, &appManager);

    eventBus.subscribe(ersa::events::EventType::MediaStateChanged, [](const ersa::events::Event&, void* user) {
        auto* mgr = static_cast<ersa::app::ApplicationManager*>(user);
        if (mgr && mgr->getActiveApp()) {
            const char* id = mgr->getActiveApp()->getId();
            if (strcmp(id, "app_media") == 0) {
                mgr->markDirty(false);
            }
        }
    }, &appManager);

    // Auto-switch to notification screen on incoming message (unless currently on a call)
    eventBus.subscribe(ersa::events::EventType::NotificationReceived, [](const ersa::events::Event&, void* user) {
        auto* mgr = static_cast<ersa::app::ApplicationManager*>(user);
        if (mgr && mgr->getActiveApp()) {
            const char* id = mgr->getActiveApp()->getId();
            if (strcmp(id, "app_call") != 0 &&
                bluetoothManager.getCallState() != ersa::services::CallState::Incoming &&
                bluetoothManager.getCallState() != ersa::services::CallState::Active) {
                mgr->switchTo("app_notifications");
                mgr->markDirty(false);
            }
        }
    }, &appManager);

    for (auto type : {ersa::events::EventType::CallEnded, ersa::events::EventType::CallAccepted,
                      ersa::events::EventType::BleDisconnected,
                      ersa::events::EventType::CompanionConnected,
                      ersa::events::EventType::CompanionDisconnected}) {
        eventBus.subscribe(type, [](const ersa::events::Event&, void* user) {
            auto* mgr = static_cast<ersa::app::ApplicationManager*>(user);
            if (!mgr || !mgr->getActiveApp()) return;
            const char* id = mgr->getActiveApp()->getId();
            if (strcmp(id, "app_call") == 0) mgr->markDirty(false);
            else if (strcmp(id, "watchface_clock") == 0) invalidateWatchface(ersa::Rect{0, 128, 200, 32});
        }, &appManager);
    }

    eventBus.subscribe(ersa::events::EventType::NotificationRemoved, [](const ersa::events::Event&, void* user) {
        auto* mgr = static_cast<ersa::app::ApplicationManager*>(user);
        if (mgr && mgr->getActiveApp() && strcmp(mgr->getActiveApp()->getId(), "app_notifications") == 0)
            mgr->markDirty(false);
    }, &appManager);
    eventBus.subscribe(ersa::events::EventType::NotificationsCleared, [](const ersa::events::Event&, void* user) {
        auto* mgr = static_cast<ersa::app::ApplicationManager*>(user);
        if (mgr && mgr->getActiveApp() && strcmp(mgr->getActiveApp()->getId(), "app_notifications") == 0)
            mgr->markDirty(false);
    }, &appManager);
    eventBus.subscribe(ersa::events::EventType::BleConnected, [](const ersa::events::Event&, void* user) {
        auto* mgr = static_cast<ersa::app::ApplicationManager*>(user);
        if (mgr && mgr->getActiveApp() && strcmp(mgr->getActiveApp()->getId(), "app_status") == 0)
            mgr->markDirty(false);
    }, &appManager);
    for (auto type : {ersa::events::EventType::CompanionConnected,
                      ersa::events::EventType::CompanionDisconnected}) {
        eventBus.subscribe(type, [](const ersa::events::Event&, void* user) {
            auto* mgr = static_cast<ersa::app::ApplicationManager*>(user);
            if (mgr && mgr->getActiveApp() && strcmp(mgr->getActiveApp()->getId(), "app_status") == 0)
                mgr->markDirty(false);
        }, &appManager);
    }

    ersa::app::registerAllApps(appManager);
    appManager.switchTo("watchface_clock");

    NetSync::begin();

    renderCurrentApp();
    lastActivityMs = board.getUptimeMs();
    lastUserInputMs = lastActivityMs;
    DebugLog::log("UI: boot complete, active app: %s",
                  appManager.getActiveApp() ? appManager.getActiveApp()->getTitle() : "none");
    DebugLog::log("BOOT CHECK summary ok=%u degraded=%u failed=%u",
                  unsigned(initOk), unsigned(initDegraded), unsigned(initFailed));
}

void WatchUi::tick() {
    bluetoothManager.tick();
    const uint32_t buttonNow = board.getUptimeMs();
    const bool topPressed = board.getInput().isPressed(ersa::events::ButtonId::Button1);
    const bool bottomPressed = board.getInput().isPressed(ersa::events::ButtonId::Button2);
    if (topPressed && bottomPressed) {
        if (!bothButtonsPressedAt) bothButtonsPressedAt = buttonNow ? buttonNow : 1;
        if (!bothButtonResetTriggered &&
            uint32_t(buttonNow - bothButtonsPressedAt) >= BOTH_BUTTON_RESET_HOLD_MS) {
            bothButtonResetTriggered = true;
            DebugLog::log("BUTTON: both held for %lu ms; forcing restart",
                          static_cast<unsigned long>(BOTH_BUTTON_RESET_HOLD_MS));
            board.restart();
        }
    } else {
        bothButtonsPressedAt = 0;
        bothButtonResetTriggered = false;
    }
    board.getInput().poll();
    if (board.getInput().isPressed(ersa::events::ButtonId::Button1) ||
        board.getInput().isPressed(ersa::events::ButtonId::Button2)) {
        lastActivityMs = board.getUptimeMs();
        lastUserInputMs = lastActivityMs;
        displayManager.noteActivity(lastActivityMs);
        powerManager.noteActivity(lastActivityMs);
        powerHal.allowAutomaticSleep(false);
    }
    eventBus.dispatchQueue();
    appManager.tick();

    uint32_t nowMs = board.getUptimeMs();
    timeService.tick(nowMs);
    NetSync::tick();
    if (!bootTimeFallbackConsidered && nowMs >= 15000) {
        if (timeService.hasSynchronizedTime()) {
            bootTimeFallbackConsidered = true;
        } else if (NetSync::startBootTimeSync()) {
            bootTimeFallbackConsidered = true;
        }
    }
    powerManager.tick(nowMs);
    displayManager.tick(nowMs);
    ersa::services::SessionStats::tick(nowMs);

    if (powerManager.getBatteryPowerLevel() == ersa::services::BatteryPowerLevel::Critical) {
        DebugLog::log("PWR: battery critical (%u mV, %u%%); entering charge-check sleep",
                      powerManager.getBatteryMv(), powerManager.getBatteryPercent());
        board.getBluetooth().stopAdvertising();
        // Wake periodically so connecting a charger can recover the watch, or
        // immediately when either hardware button is pressed.
        powerManager.enterDeepSleep(10ULL * 60ULL * 1000000ULL);
    }

    const uint32_t idleMs = (nowMs >= lastActivityMs) ? (nowMs - lastActivityMs) : 0;
    const uint32_t userIdleMs = (nowMs >= lastUserInputMs) ? (nowMs - lastUserInputMs) : 0;

    // Auto-return to watchface after 60 seconds of inactivity on other screens (except during calls)
    if (appManager.getActiveApp() != nullptr &&
        strcmp(appManager.getActiveApp()->getId(), "watchface_clock") != 0 &&
        strcmp(appManager.getActiveApp()->getId(), "app_call") != 0 &&
        (idleMs >= 60000)) {
        DebugLog::log("UI: auto-returning to watchface after 60s idle");
        appManager.switchTo("watchface_clock");
        lastActivityMs = nowMs;
    }

    const uint32_t currentMinute = WatchClock::now().unixtime() / 60;
    if (currentMinute != shownMinute || WatchClock::healthy() != shownRtcHealthy) {
        if (appManager.getActiveApp() && strcmp(appManager.getActiveApp()->getId(), "watchface_clock") == 0)
            invalidateWatchface(ersa::Rect{0, 20, 200, 104});
    }

    // AMS reports title, artist and playback independently during discovery.
    // Wait briefly for that burst, then refresh their shared complication area once.
    if (watchfaceMediaPending && uint32_t(nowMs - watchfaceMediaChangedAt) >= 500) {
        watchfaceMediaPending = false;
        if (appManager.getActiveApp() && strcmp(appManager.getActiveApp()->getId(), "watchface_clock") == 0)
            invalidateWatchface(ersa::Rect{0, 128, 200, 32});
    }

    const bool displayBusy = board.getDisplay().isBusy();
    const uint32_t timeSinceRender = (nowMs >= lastFrameEnd) ? (nowMs - lastFrameEnd) : 0;
    if (appManager.isDirty() && !displayBusy && (timeSinceRender >= 20)) {
        renderCurrentApp();
        appManager.clearDirty();
        nowMs = board.getUptimeMs();
    }

    const auto* currentApp = appManager.getActiveApp();
    const auto callState = bluetoothManager.getCallState();
    const bool appBusy = (currentApp && strcmp(currentApp->getId(), "app_portal") == 0) ||
        callState == ersa::services::CallState::Incoming || callState == ersa::services::CallState::Active;
    // Passive BLE traffic must wake/process the UI but must not continually
    // restart the inactivity timer. Buttons represent deliberate use; the
    // BLE callback notification wakes this task independently.
    // Keep the platform no-sleep lock while a USB host is attached; the
    // selected console HAL knows whether its transport survives light sleep.
    const bool usbConsoleAttached = board.getConsole().isAttached();
    const ersa::runtime::SleepEligibility sleepPolicy{
        automaticSleepReady,
        usbConsoleAttached,
        userIdleMs >= 30000,
        appBusy,
        NetSync::isSyncing(),
        board.getDisplay().isBusy(),
        !powerManager.canSleep(),
        board.getInput().hasPendingEvents() || appManager.isDirty()
    };
    const bool sleepEligible = sleepPolicy.maySleep();
    powerHal.allowAutomaticSleep(sleepEligible);

    if (sleepEligible) {
        powerHal.reportPowerModes();
        // Sleep until the earliest real deadline: minute refresh, pending media
        // redraw, or a scheduled BLE advertising retry.
        const uint32_t waitNowMs = board.getUptimeMs();
        const uint32_t secondsToMinute = 60 - WatchClock::now().second();
        ersa::runtime::WakeDeadlineSet deadlines(secondsToMinute * 1000UL);
        const uint32_t bleWaitMs = bluetoothManager.nextWakeDelayMs(board.getUptimeMs());
        deadlines.includeDelay(bleWaitMs);
        const uint32_t batteryWaitMs = powerManager.nextBatterySampleDelayMs(board.getUptimeMs());
        deadlines.includeDelay(batteryWaitMs);
        // Keep UI-owned deadlines live even when the event loop is otherwise
        // idle: boot time fallback and returning transient screens must not
        // wait for the next minute or battery sample.
        if (!bootTimeFallbackConsidered && waitNowMs < 15000) {
            deadlines.includeDelay(15000 - waitNowMs);
        }
        if (currentApp && strcmp(currentApp->getId(), "watchface_clock") != 0 &&
            strcmp(currentApp->getId(), "app_call") != 0 && idleMs < 60000) {
            deadlines.includeDelay(60000 - idleMs);
        }
        if (watchfaceMediaPending) {
            const uint32_t elapsed = uint32_t(waitNowMs - watchfaceMediaChangedAt);
            const uint32_t mediaWaitMs = elapsed >= 500 ? 0 : 500 - elapsed;
            deadlines.includeDelay(mediaWaitMs);
        }
        powerHal.waitForWake(deadlines.delayMs());
        // board.getUptimeMs() can pause during light sleep on this target. Re-anchor the
        // software clock to the DS3231 before calculating the next refresh.
        WatchClock::resync();
    } else if (!appManager.isDirty() && !board.getInput().hasPendingEvents()) {
        board.delayMs(25);
    } else {
        board.delayMs(1);  // Ultra-fast 1ms loop response during button/UI interaction
    }
}
