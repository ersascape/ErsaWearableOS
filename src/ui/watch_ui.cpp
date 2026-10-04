#include "ui/watch_ui.h"
#include "bsp/terra/board_terra.h"
#include "ersa/events/event_bus.h"
#include "ersa/app/application_manager.h"
#include "ersa/services/time_service.h"
#include "ersa/services/power_manager.h"
#include "ersa/services/display_manager.h"
#include "ersa/services/bluetooth_manager.h"
#include "ersa/services/session_stats.h"
#include "apps/apps_registry.h"
#include "apps/app_portal.h"
#include "core/watch_clock.h"
#include "core/debug_log.h"
#include "core/buttons.h"
#include "core/net_sync.h"
#include "core/dvfs.h"
#include <Arduino.h>
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE && defined(CONFIG_FREERTOS_USE_TICKLESS_IDLE) && CONFIG_FREERTOS_USE_TICKLESS_IDLE
#include <esp_pm.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "board_pins.h"
#endif

namespace {

ersa::board::BoardTerra& board = ersa::board::BoardTerra::instance();
ersa::events::EventBus& eventBus = ersa::events::EventBus::instance();
ersa::app::ApplicationManager& appManager = ersa::app::ApplicationManager::instance();

ersa::services::TimeService timeService(board.getRtc(), eventBus);
ersa::services::PowerManager powerManager(board.getBattery(), eventBus);
ersa::services::DisplayManager displayManager(board.getDisplay());
ersa::services::BluetoothManager bluetoothManager(board.getBluetooth(), board.getCompanionSource(), eventBus);

uint32_t shownMinute = UINT32_MAX;
uint8_t shownDay = 0;
bool shownRtcHealthy = false;
uint32_t lastFrameEnd = 0;
uint32_t lastActivityMs = 0;
uint32_t lastUserInputMs = 0;
uint16_t partialFrames = 0;
bool firstFrame = true;
bool watchfaceRectPending = false;
ersa::Rect watchfaceRect{0, 0, 0, 0};
bool watchfaceMediaPending = false;
uint32_t watchfaceMediaChangedAt = 0;
char renderedWatchfaceMediaTitle[32] = {0};
uint32_t bothButtonsPressedAt = 0;
bool bothButtonResetTriggered = false;
constexpr uint32_t BOTH_BUTTON_RESET_HOLD_MS = 3000;

#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE && defined(CONFIG_FREERTOS_USE_TICKLESS_IDLE) && CONFIG_FREERTOS_USE_TICKLESS_IDLE
TaskHandle_t uiTaskHandle = nullptr;
esp_pm_lock_handle_t uiNoSleepLock = nullptr;
bool automaticSleepReady = false;
bool noSleepLockHeld = false;

void notifyUiTask(void*) {
    if (uiTaskHandle) xTaskNotifyGive(uiTaskHandle);
}

void IRAM_ATTR buttonWakeIsr() {
    if (!uiTaskHandle) return;
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(uiTaskHandle, &higherPriorityTaskWoken);
    if (higherPriorityTaskWoken) portYIELD_FROM_ISR();
}

void setSleepAllowed(bool allowed) {
    if (!automaticSleepReady || !uiNoSleepLock) return;
    if (allowed && noSleepLockHeld) {
        esp_pm_lock_release(uiNoSleepLock);
        noSleepLockHeld = false;
    } else if (!allowed && !noSleepLockHeld) {
        esp_pm_lock_acquire(uiNoSleepLock);
        noSleepLockHeld = true;
    }
}
#else
bool automaticSleepReady = false;
void notifyUiTask(void*) {}
void setSleepAllowed(bool) {}
#endif

void queueWatchfaceMediaRefresh() {
    watchfaceMediaPending = true;
    watchfaceMediaChangedAt = millis();
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

ersa::events::Event toErsaInputEvent(Buttons::Event legacy) {
    using namespace ersa::events;
    switch (legacy) {
        case Buttons::Event::Next:
            return Event::createButton(EventType::ButtonClicked, ButtonId::Button1, millis());
        case Buttons::Event::Previous:
            return Event::createButton(EventType::ButtonDoubleClicked, ButtonId::Button1, millis());
        case Buttons::Event::Home:
            return Event::createButton(EventType::ButtonLongPressed, ButtonId::Button1, millis());
        case Buttons::Event::Action:
            return Event::createButton(EventType::ButtonClicked, ButtonId::Button2, millis());
        case Buttons::Event::ActionAlt:
            return Event::createButton(EventType::ButtonDoubleClicked, ButtonId::Button2, millis());
        case Buttons::Event::ActionLong:
            return Event::createButton(EventType::ButtonLongPressed, ButtonId::Button2, millis());
        default:
            return Event();
    }
}

void renderCurrentApp() {
    const uint32_t started = millis();
    const DateTime time = WatchClock::now();
    auto* activeApp = appManager.getActiveApp();
    if (!activeApp) return;

    auto& espDisp = board.getEsp32Display();
    auto& gx = espDisp.getGxDisplay();

    // 1. Hardware full refresh (waveform clear): ONLY on firstFrame after boot, or once every 360 partial updates (~6 hours), or day change at midnight.
    // NEVER on button press, scroll, or normal minute ticks!
    const bool appSwitched = appManager.isAppSwitched();
    const bool dayChanged = (shownDay != 0 && time.day() != shownDay);
    const bool hardwareFull = firstFrame || (partialFrames >= 360) || dayChanged;

    DebugLog::log("EPD begin app=%s hwFull=%d time=%02u:%02u:%02u",
                  activeApp->getId(), hardwareFull,
                  unsigned(time.hour()), unsigned(time.minute()), unsigned(time.second()));

    gx.fillScreen(GxEPD_BLACK);
    gx.setTextColor(GxEPD_WHITE);
    gx.setTextWrap(false);
    {
        Dvfs::Scope frequency(Dvfs::Profile::Compute, "app-render");
        activeApp->render(espDisp, true);
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

    gx.setFullWindow();
    if (hardwareFull) {
        espDisp.refresh(true); // Hardware full refresh (clears ghosting)
        partialFrames = 0;
        firstFrame = false;
    } else if (appSwitched) {
        // App transitions replace the whole page. Refresh the whole panel with
        // the partial waveform so stale drawer/footer pixels cannot survive.
        espDisp.refreshRect(ersa::Rect{0, 0, espDisp.width(), espDisp.height()});
        partialFrames++;
    } else {
        // Let the display HAL honor the app's invalidated area. The old path
        // sent a 200x200 partial update for every redraw, flashing the entire
        // panel even for routine watchface changes.
        if (strcmp(activeApp->getId(), "watchface_clock") == 0 && watchfaceRectPending) {
            espDisp.refreshRect(watchfaceRect);
        } else {
            espDisp.refreshRect(activeApp->getPartialBounds());
        }
        partialFrames++;
    }
    watchfaceRectPending = false;
    appManager.clearAppSwitched();

    lastActivityMs = millis();
    shownMinute = time.unixtime() / 60;
    shownDay = time.day();
    shownRtcHealthy = WatchClock::healthy();
    lastFrameEnd = millis();

    DebugLog::log("EPD end duration=%lu ms BUSY=%d (partialFrames=%u)",
                  (unsigned long)(lastFrameEnd - started), espDisp.isBusy(), partialFrames);
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

#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE && defined(CONFIG_FREERTOS_USE_TICKLESS_IDLE) && CONFIG_FREERTOS_USE_TICKLESS_IDLE
    uiTaskHandle = xTaskGetCurrentTaskHandle();
    if (esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "ui-active", &uiNoSleepLock) == ESP_OK &&
        esp_pm_lock_acquire(uiNoSleepLock) == ESP_OK) {
        automaticSleepReady = true;
        noSleepLockHeld = true;
        gpio_wakeup_enable(static_cast<gpio_num_t>(Pins::BUTTON_1), GPIO_INTR_LOW_LEVEL);
        gpio_wakeup_enable(static_cast<gpio_num_t>(Pins::BUTTON_2), GPIO_INTR_LOW_LEVEL);
        esp_sleep_enable_gpio_wakeup();
        attachInterrupt(digitalPinToInterrupt(Pins::BUTTON_1), buttonWakeIsr, CHANGE);
        attachInterrupt(digitalPinToInterrupt(Pins::BUTTON_2), buttonWakeIsr, CHANGE);
        DebugLog::log("PWR: light sleep armed after 30s inactivity; wake sources BLE, minute timer, buttons");
    } else {
        DebugLog::log("PWR: cannot create active-state sleep lock");
    }
#endif
    bluetoothManager.setWakeCallback(notifyUiTask, nullptr);

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
            const uint32_t now = millis();
            lastActivityMs = now;
            displayManager.noteActivity(now);
            powerManager.noteActivity(now);
            setSleepAllowed(false);
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
    lastActivityMs = millis();
    lastUserInputMs = lastActivityMs;
    DebugLog::log("UI: boot complete, active app: %s",
                  appManager.getActiveApp() ? appManager.getActiveApp()->getTitle() : "none");
    DebugLog::log("BOOT CHECK summary ok=%u degraded=%u failed=%u",
                  unsigned(initOk), unsigned(initDegraded), unsigned(initFailed));
}

void WatchUi::onButton(Buttons::Event legacyEvent) {
    if (legacyEvent == Buttons::Event::None) return;

    lastActivityMs = millis();
    lastUserInputMs = lastActivityMs;
    displayManager.noteActivity(lastActivityMs);
    powerManager.noteActivity(lastActivityMs);
    setSleepAllowed(false);

    // Quick dial on watchface: holding B1 dials top recent contact
    if (appManager.getActiveApp() != nullptr &&
        strcmp(appManager.getActiveApp()->getId(), "watchface_clock") == 0 &&
        legacyEvent == Buttons::Event::Home) {
        if (bluetoothManager.canDial() && bluetoothManager.getRecentCallCount() > 0) {
            DebugLog::log("UI: Hold B1 on watchface -> Quick dial recent %s (%s)",
                          bluetoothManager.getRecentCall(0).name,
                          bluetoothManager.getRecentCall(0).number);
            bluetoothManager.dialRecent(0);
            appManager.switchTo("app_call");
            appManager.markDirty(false);
            return;
        }
    }

    const ersa::events::Event evt = toErsaInputEvent(legacyEvent);
    const bool handled = appManager.handleEvent(evt);

    if (handled) {
        appManager.markDirty(false);
    }
}

void WatchUi::tick() {
    bluetoothManager.tick();
    const uint32_t buttonNow = millis();
    if (Buttons::bothPressed()) {
        if (!bothButtonsPressedAt) bothButtonsPressedAt = buttonNow ? buttonNow : 1;
        if (!bothButtonResetTriggered &&
            uint32_t(buttonNow - bothButtonsPressedAt) >= BOTH_BUTTON_RESET_HOLD_MS) {
            bothButtonResetTriggered = true;
            DebugLog::log("BUTTON: both held for %lu ms; forcing restart",
                          static_cast<unsigned long>(BOTH_BUTTON_RESET_HOLD_MS));
            ESP.restart();
        }
    } else {
        bothButtonsPressedAt = 0;
        bothButtonResetTriggered = false;
    }
    board.getInput().poll();
    if (Buttons::isPressed()) {
        lastActivityMs = millis();
        lastUserInputMs = lastActivityMs;
        displayManager.noteActivity(lastActivityMs);
        powerManager.noteActivity(lastActivityMs);
        setSleepAllowed(false);
    }
    eventBus.dispatchQueue();
    appManager.tick();

    uint32_t nowMs = millis();
    timeService.tick(nowMs);
    powerManager.tick(nowMs);
    ersa::services::SessionStats::tick(nowMs);

    if (powerManager.getBatteryPowerLevel() == ersa::services::BatteryPowerLevel::Critical) {
        DebugLog::log("PWR: battery critical (%u mV, %u%%); entering charge-check sleep",
                      powerManager.getBatteryMv(), powerManager.getBatteryPercent());
        board.getEsp32Bluetooth().stopAdvertising();
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

    const bool displayBusy = board.getEsp32Display().isBusy();
    const uint32_t timeSinceRender = (nowMs >= lastFrameEnd) ? (nowMs - lastFrameEnd) : 0;
    if (appManager.isDirty() && !displayBusy && (timeSinceRender >= 20)) {
        renderCurrentApp();
        appManager.clearDirty();
        nowMs = millis();
    }

    const auto* currentApp = appManager.getActiveApp();
    const auto callState = bluetoothManager.getCallState();
    const bool appBusy = (currentApp && strcmp(currentApp->getId(), "app_portal") == 0) ||
        callState == ersa::services::CallState::Incoming || callState == ersa::services::CallState::Active;
    // Passive BLE traffic must wake/process the UI but must not continually
    // restart the inactivity timer. Buttons represent deliberate use; the
    // BLE callback notification wakes this task independently.
    // On ESP32-C3 the USB Serial/JTAG PHY loses its APB/USB clock during
    // automatic light sleep. Keep the UI's NO_LIGHT_SLEEP lock while a host
    // is attached so the console doesn't disappear until the cable/monitor
    // is disconnected. Arduino's HWCDC Serial bool reflects CDC connection.
    const bool usbConsoleAttached = bool(Serial);
    const bool sleepEligible = automaticSleepReady && !usbConsoleAttached &&
                               userIdleMs >= 30000 && !appBusy &&
                               !NetSync::isSyncing() && !board.getEsp32Display().isBusy();
    setSleepAllowed(sleepEligible);

    if (!appManager.isDirty() && !Buttons::hasPendingEvents() && sleepEligible) {
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE && defined(CONFIG_FREERTOS_USE_TICKLESS_IDLE) && CONFIG_FREERTOS_USE_TICKLESS_IDLE
        Dvfs::reportPowerModes();
        // Sleep until the earliest real deadline: minute refresh, pending media
        // redraw, or a scheduled BLE advertising retry.
        const uint32_t secondsToMinute = 60 - WatchClock::now().second();
        uint32_t waitMs = secondsToMinute * 1000UL;
        const uint32_t bleWaitMs = bluetoothManager.nextWakeDelayMs(millis());
        if (bleWaitMs < waitMs) waitMs = bleWaitMs;
        const uint32_t batteryWaitMs = powerManager.nextBatterySampleDelayMs(millis());
        if (batteryWaitMs < waitMs) waitMs = batteryWaitMs;
        if (watchfaceMediaPending) {
            const uint32_t elapsed = uint32_t(millis() - watchfaceMediaChangedAt);
            const uint32_t mediaWaitMs = elapsed >= 500 ? 0 : 500 - elapsed;
            if (mediaWaitMs < waitMs) waitMs = mediaWaitMs;
        }
        TickType_t waitTicks = pdMS_TO_TICKS(waitMs);
        if (!waitTicks) waitTicks = 1;
        ulTaskNotifyTake(pdTRUE, waitTicks ? waitTicks : 1);
        // millis() can pause during light sleep on this target. Re-anchor the
        // software clock to the DS3231 before calculating the next refresh.
        WatchClock::resync();
#else
        delay(25);
#endif
    } else if (!appManager.isDirty() && !Buttons::hasPendingEvents()) {
        delay(25);
    } else {
        delay(1);  // Ultra-fast 1ms loop response during button/UI interaction
    }
}
