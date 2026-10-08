#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "ersa/common/types.h"
#include "ersa/events/event_bus.h"
#include "ersa/app/application_manager.h"
#include "ersa/app/watchface.h"
#include "ersa/app/complication.h"
#include "ersa/services/time_service.h"
#include "ersa/services/power_manager.h"
#include "ersa/services/display_manager.h"
#include "ersa/services/network_manager.h"
#include "ersa/services/storage_service.h"
#include "ersa/services/settings_service.h"
#include "ersa/services/logging_service.h"
#include "ersa/services/bluetooth_manager.h"
#include "ersa/services/session_stats.h"
#include "ersa/runtime/scheduler.h"
#include "ersa/system.h"
#include "mocks/mock_display.h"
#include "mocks/mock_rtc.h"
#include "mocks/mock_battery.h"
#include "mocks/mock_bluetooth.h"
#include "mocks/mock_wifi.h"
#include "mocks/mock_power_management.h"
#include "mocks/mock_console.h"

using namespace ersa;

static int testsPassed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAILED: %s (line %d): %s\n", __func__, __LINE__, msg); \
            assert(cond); \
        } \
    } while(0)

#define TEST_PASS() \
    do { \
        testsPassed++; \
        printf("PASS: %s\n", __func__); \
    } while(0)

void test_display_hal_contract() {
    test::MockDisplay implementation;
    hal::IDisplay& display = implementation;

    TEST_ASSERT(display.init().isOk(), "Generic display contract initializes");
    TEST_ASSERT(display.width() == 200 && display.height() == 200,
                "Display geometry is available through the generic interface");
    display.drawLine(1, 2, 3, 4, hal::Color::White);
    display.setFont(hal::FontFace::MiSansBold10);
    display.setCursor(12, 34);
    display.setTextColor(hal::Color::Black);
    display.print("generic display");
    TEST_ASSERT(implementation.drawLineCalls_ == 1,
                "Drawing dispatches through IDisplay without a concrete driver cast");
    TEST_ASSERT(implementation.font_ == hal::FontFace::MiSansBold10 &&
                implementation.cursorX_ == 12 && implementation.cursorY_ == 34,
                "Logical font and cursor settings reach the implementation");
    TEST_ASSERT(implementation.lastText_ == "generic display",
                "Text is accepted through the generic display contract");

    TEST_PASS();
}

void test_runtime_scheduler() {
    runtime::SleepEligibility eligible{true, false, true, false, false, false, false, false};
    TEST_ASSERT(eligible.maySleep(), "sleep is allowed when all owners are idle");
    eligible.featureLeaseHeld = true;
    TEST_ASSERT(!eligible.maySleep(), "a feature lease blocks automatic sleep");
    eligible.featureLeaseHeld = false;
    eligible.pendingWork = true;
    TEST_ASSERT(!eligible.maySleep(), "pending work blocks automatic sleep");
    eligible.pendingWork = false;
    eligible.foregroundWorkActive = true;
    TEST_ASSERT(!eligible.maySleep(), "active foreground work blocks automatic sleep");

    runtime::WakeDeadlineSet deadlines(60000);
    deadlines.includeDelay(20000);
    deadlines.includeDelay(500);
    deadlines.includeDelay(10000);
    TEST_ASSERT(deadlines.delayMs() == 500, "scheduler selects the earliest service deadline");
    deadlines.includeDelay(0);
    TEST_ASSERT(deadlines.delayMs() == 0, "due work requests an immediate wake");

    eligible.foregroundWorkActive = false;
    const auto sleepingPlan = runtime::RuntimeScheduler::plan(eligible, false, false, 1200);
    TEST_ASSERT(sleepingPlan.allowAutomaticSleep && sleepingPlan.waitMs == 1200,
                "idle loop waits to the earliest deadline with automatic sleep enabled");
    eligible.consoleAttached = true;
    const auto idlePlan = runtime::RuntimeScheduler::plan(eligible, false, false, 1200);
    TEST_ASSERT(!idlePlan.allowAutomaticSleep && idlePlan.waitMs == 25,
                "awake but quiescent loop uses relaxed polling cadence");
    const auto activePlan = runtime::RuntimeScheduler::plan(eligible, true, false, 1200);
    TEST_ASSERT(!activePlan.allowAutomaticSleep && activePlan.waitMs == 1,
                "pending UI work uses responsive polling cadence");
    TEST_PASS();
}

void test_console_hal_contract() {
    test::MockConsole implementation;
    hal::IConsole& console = implementation;
    console.begin(115200);
    const uint8_t message[] = {'o', 'k'};
    TEST_ASSERT(console.isAttached() && implementation.baudRate_ == 115200,
                "console exposes attachment state and configured transport speed");
    TEST_ASSERT(console.write(message, sizeof(message)) == sizeof(message) &&
                implementation.output_ == "ok", "console writes through the generic byte contract");
    implementation.input_ = "x";
    TEST_ASSERT(console.available() == 1 && console.read() == 'x',
                "console exposes bounded host input through the generic contract");
    console.flush();
    TEST_ASSERT(implementation.flushCount_ == 1, "console flush request reaches its backend");
    TEST_PASS();
}

void test_power_management_hal_contract() {
    test::MockPowerManagement implementation;
    hal::IPowerManagement& power = implementation;
    power.allowAutomaticSleep(true);
    power.waitForWake(1200);
    power.notifyWake();
    power.enterDeepSleep(900000);
    {
        hal::PerformanceScope scope(power, hal::PerformanceProfile::Compute, "test");
        TEST_ASSERT(implementation.performanceAcquires == 1, "performance scope acquires through HAL");
    }
    TEST_ASSERT(implementation.sleepAllowed && implementation.lastWaitMs == 1200,
                "sleep policy and timed wake wait are exposed by HAL");
    TEST_ASSERT(implementation.wakeNotifications == 1 && implementation.lastDeepSleepUs == 900000,
                "wake and deep-sleep requests reach HAL");
    TEST_ASSERT(implementation.performanceReleases == 1, "performance scope releases through HAL");
    TEST_PASS();
}

// -----------------------------------------------------------------------------
// Test EventBus with EventType
// -----------------------------------------------------------------------------
static int g_subCallCount = 0;
static events::Event g_lastReceivedEvent;

static void onTestEvent(const events::Event& event, void* userData) {
    (void)userData;
    g_subCallCount++;
    g_lastReceivedEvent = event;
}

void test_event_bus() {
    events::EventBus bus;
    g_subCallCount = 0;

    // 1. Subscribe to ButtonClicked
    events::SubscriptionId sub1 = bus.subscribe(events::EventType::ButtonClicked, onTestEvent);
    TEST_ASSERT(sub1 != 0, "Subscription failed");

    // 2. Publish MinuteTick (should NOT trigger sub1)
    events::TimePayload tp{100, 2026, 9, 28, 12, 0, 0};
    bus.publish(events::Event::createMinuteTick(tp, 1000));
    TEST_ASSERT(g_subCallCount == 0, "Button subscriber should not receive time event");

    // 3. Publish ButtonClicked (should trigger sub1)
    bus.publish(events::Event::createButton(events::EventType::ButtonClicked, events::ButtonId::Button1, 1050));
    TEST_ASSERT(g_subCallCount == 1, "Button subscriber should receive event");
    TEST_ASSERT(g_lastReceivedEvent.type == events::EventType::ButtonClicked, "Type match");
    TEST_ASSERT(g_lastReceivedEvent.button.button == events::ButtonId::Button1, "Button match");

    // 4. Test Queue and dispatch
    bus.post(events::Event::createButton(events::EventType::ButtonClicked, events::ButtonId::Button2, 2000));
    TEST_ASSERT(g_subCallCount == 1, "Queued event should not immediately trigger");
    size_t processed = bus.dispatchQueue();
    TEST_ASSERT(processed == 1, "Processed 1 queued event");
    TEST_ASSERT(g_subCallCount == 2, "Subscriber called after dispatchQueue");
    TEST_ASSERT(g_lastReceivedEvent.button.button == events::ButtonId::Button2, "Button 2 match");

    // 5. Unsubscribe
    bool unsub = bus.unsubscribe(sub1);
    TEST_ASSERT(unsub, "Unsubscribe succeeded");
    bus.publish(events::Event::createButton(events::EventType::ButtonClicked, events::ButtonId::Button1, 3000));
    TEST_ASSERT(g_subCallCount == 2, "Subscriber should not be called after unsubscribing");

    TEST_PASS();
}

// -----------------------------------------------------------------------------
// Test Application & ApplicationManager Lifecycle
// -----------------------------------------------------------------------------
class TestAppA : public app::Application {
public:
    int createCount{0};
    int startCount{0};
    int resumeCount{0};
    int pauseCount{0};
    int stopCount{0};
    int eventCount{0};
    int renderCount{0};

    const char* getId() const override { return "app_a"; }
    const char* getTitle() const override { return "Application A"; }

    void onCreate() override { createCount++; }
    void onStart() override { startCount++; }
    void onResume() override { resumeCount++; }
    void onPause() override { pauseCount++; }
    void onStop() override { stopCount++; }

    void onEvent(const events::Event& event) override {
        (void)event;
        eventCount++;
    }

    void render(hal::IDisplay& display, bool full) override {
        (void)full;
        renderCount++;
        display.fillRect(0, 0, 10, 10, hal::Color::White);
    }
};

class TestAppB : public app::Application {
public:
    int createCount{0};
    int startCount{0};
    int resumeCount{0};

    const char* getId() const override { return "app_b"; }
    const char* getTitle() const override { return "Application B"; }

    void onCreate() override { createCount++; }
    void onStart() override { startCount++; }
    void onResume() override { resumeCount++; }
};

void test_application_manager() {
    app::ApplicationManager mgr;
    test::MockDisplay display;

    TestAppA appA;
    TestAppB appB;

    // 1. Register apps (triggers onCreate)
    TEST_ASSERT(mgr.registerApp(&appA), "App A registered");
    TEST_ASSERT(appA.createCount == 1, "App A onCreate called");
    TEST_ASSERT(appA.startCount == 1, "App A onStart called as first app");
    TEST_ASSERT(appA.resumeCount == 1, "App A onResume called as first app");

    TEST_ASSERT(mgr.registerApp(&appB), "App B registered");
    TEST_ASSERT(appB.createCount == 1, "App B onCreate called");
    TEST_ASSERT(appB.startCount == 0, "App B not started yet");
    TEST_ASSERT(mgr.getAppCount() == 2, "Two apps registered");
    mgr.clearDirty();

    // 2. Event routing
    events::Event evt = events::Event::createButton(events::EventType::ButtonClicked, events::ButtonId::Button1);
    TEST_ASSERT(mgr.handleEvent(evt), "App handled event");
    TEST_ASSERT(appA.eventCount == 1, "Event reached active App A");
    TEST_ASSERT(!mgr.isDirty(), "Unrelated event does not force a display redraw");
    mgr.markDirty(false);

    // 3. Render
    mgr.render(display);
    TEST_ASSERT(appA.renderCount == 1, "App A rendered");
    TEST_ASSERT(!mgr.isDirty(), "Manager not dirty after render");
    TEST_ASSERT(display.getPixel(5, 5) == hal::Color::White, "Pixel drawn by app");

    // 4. Switch app (appA pauses/stops, appB starts/resumes)
    TEST_ASSERT(mgr.switchTo("app_b"), "Switched to App B");
    TEST_ASSERT(appA.pauseCount == 1, "App A paused");
    TEST_ASSERT(appA.stopCount == 1, "App A stopped");
    TEST_ASSERT(appB.startCount == 1, "App B started");
    TEST_ASSERT(appB.resumeCount == 1, "App B resumed");
    TEST_ASSERT(mgr.getActiveApp() == &appB, "App B is now active");
    TEST_ASSERT(mgr.isAppSwitched(), "App switch flag is set");

    // 5. Render after app switch clears the switch flag
    mgr.render(display);
    TEST_ASSERT(!mgr.isAppSwitched(), "App switch flag cleared after render");

    // 6. Test sub-window partial refresh within same app
    mgr.handleEvent(evt);
    mgr.markDirty(false); // explicit visual invalidation without an app switch
    const uint32_t partialsBefore = display.partialRefreshes_;
    mgr.render(display);
    TEST_ASSERT(display.partialRefreshes_ == partialsBefore + 1, "Partial refresh executed");

    TEST_PASS();
}

// -----------------------------------------------------------------------------
// Test TimeService
// -----------------------------------------------------------------------------
static int g_timeEventCount = 0;
static void onTimeEvent(const events::Event& event, void* userData) {
    (void)userData;
    if (event.type == events::EventType::MinuteTick) {
        g_timeEventCount++;
    }
}

void test_time_service() {
    events::EventBus bus;
    bus.subscribe(events::EventType::MinuteTick, onTimeEvent);
    g_timeEventCount = 0;

    test::MockRtc rtc(1000000020); // Aligned minute boundary
    services::TimeService timeService(rtc, bus);

    TEST_ASSERT(timeService.init().isOk(), "TimeService init ok");
    TEST_ASSERT(timeService.isRtcHealthy(), "Mock RTC healthy");
    TEST_ASSERT(!timeService.hasSynchronizedTime(), "RTC boot time is not an external sync");

    // Initial tick
    timeService.tick(0);
    TEST_ASSERT(g_timeEventCount == 0, "No minute tick yet");

    // Advance 30 seconds: same minute
    rtc.advanceSeconds(30);
    timeService.tick(1500);
    TEST_ASSERT(g_timeEventCount == 0, "Still no minute tick");

    // Advance past minute boundary: should trigger MinuteTick on EventBus
    rtc.advanceSeconds(35);
    timeService.tick(2500);
    TEST_ASSERT(g_timeEventCount == 1, "Minute tick event dispatched on EventBus");

    // Test timezone offset
    timeService.setTimezoneOffset(330); // UTC+5:30
    TEST_ASSERT(timeService.getTimezoneOffset() == 330, "Timezone offset set");

    const uint32_t networkTime = 1790685296UL;
    const uint32_t bleTime = networkTime + 120;
    const uint32_t laterNetworkTime = networkTime + 300;
    TEST_ASSERT(timeService.submitTime(events::TimeSource::Network, networkTime), "network time accepted initially");
    TEST_ASSERT(timeService.hasSynchronizedTime(), "network time marks clock synchronized");
    TEST_ASSERT(timeService.submitTime(events::TimeSource::BleCurrentTime, bleTime), "BLE time supersedes network time");
    TEST_ASSERT(!timeService.submitTime(events::TimeSource::Network, laterNetworkTime), "network time cannot supersede BLE time");
    TEST_ASSERT(rtc.now().epoch == bleTime, "RTC retains BLE-priority time");
    TEST_ASSERT(timeService.submitTime(events::TimeSource::Manual, laterNetworkTime), "manual time has highest priority");
    TEST_ASSERT(!timeService.submitTime(events::TimeSource::BleCurrentTime, bleTime), "BLE cannot supersede manual time");

    TEST_PASS();
}

// -----------------------------------------------------------------------------
// Test PowerManager & WakeLock RAII
// -----------------------------------------------------------------------------
void test_power_manager() {
    events::EventBus bus;
    test::MockBattery battery(3800, 60, true, false);
    services::PowerManager power(battery, bus);

    TEST_ASSERT(power.init().isOk(), "PowerManager init");
    TEST_ASSERT(power.getBatteryMv() == 3800, "Voltage matches");
    TEST_ASSERT(power.getBatteryPercent() == 60, "Percentage matches");
    TEST_ASSERT(power.isBatteryConnected(), "Battery connected");
    TEST_ASSERT(!power.isCharging(), "Not charging");
    TEST_ASSERT(power.nextBatterySampleDelayMs(10000) == 10000,
                "moderate battery voltage uses a 20-second sample interval");

    battery.setMv(3300);
    battery.setPct(2);
    power.tick(10000);
    power.tick(20000);
    TEST_ASSERT(power.getBatteryPowerLevel() == services::BatteryPowerLevel::Normal, "critical voltage remains qualified across samples");
    power.tick(30000);
    TEST_ASSERT(power.getBatteryPowerLevel() == services::BatteryPowerLevel::Normal, "critical voltage requires repeated samples");
    power.tick(40000);
    TEST_ASSERT(power.getBatteryPowerLevel() == services::BatteryPowerLevel::Critical, "critical battery is latched after three samples");
    battery.setMv(3800);
    battery.setPct(60);
    power.tick(50000);
    TEST_ASSERT(power.getBatteryPowerLevel() == services::BatteryPowerLevel::Normal, "critical state clears after clear voltage recovery");

    // WakeLock RAII tests
    services::PowerManager::setInstance(&power);
    TEST_ASSERT(!power.hasWakeLocks(), "No wake locks initially");

    {
        services::WakeLock lock1 = power.acquireWakeLock("ntp-sync");
        TEST_ASSERT(power.hasWakeLocks(), "Has wake lock inside scope");
        TEST_ASSERT(power.getActiveWakeLockCount() == 1, "1 wake lock active");

        {
            services::WakeLock lock2 = power.acquireWakeLock("screen-render");
            TEST_ASSERT(power.getActiveWakeLockCount() == 2, "2 wake locks active");
        }
        TEST_ASSERT(power.getActiveWakeLockCount() == 1, "lock2 released on scope exit");
    }
    TEST_ASSERT(!power.hasWakeLocks(), "All wake locks released automatically via RAII");

    {
        services::WakeLock first = power.acquireWakeLock("shared-operation");
        services::WakeLock second = power.acquireWakeLock("shared-operation");
        TEST_ASSERT(power.getActiveWakeLockCount() == 2,
                    "same-tag wake lock acquisitions are counted independently");
        first.release();
        TEST_ASSERT(!power.canSleep() && power.getActiveWakeLockCount() == 1,
                    "releasing one same-tag lease keeps the other sleep constraint active");
    }
    TEST_ASSERT(power.canSleep(), "all same-tag leases release independently");

    // Activity tracking
    power.noteActivity(5000);
    TEST_ASSERT(power.getIdleTimeMs(5000) == 0, "Zero idle right after activity");
    TEST_ASSERT(power.getIdleTimeMs(12000) == 7000, "Idle time computed correctly");

    // Sleep tests
    TEST_ASSERT(power.canSleep(), "Can sleep without wake locks");
    {
        services::WakeLock lock = power.acquireWakeLock("wifi");
        TEST_ASSERT(!power.canSleep(), "Cannot sleep while wake lock held");
    }
    TEST_ASSERT(power.canSleep(), "Can sleep after wake lock released");

    power.enterDeepSleep(0);
    TEST_ASSERT(power.getState() == services::PowerState::DeepSleep, "State changed to DeepSleep");

    TEST_PASS();
}

// -----------------------------------------------------------------------------
// Test DisplayManager
// -----------------------------------------------------------------------------
void test_display_manager() {
    test::MockDisplay display;
    services::DisplayManager displayMgr(display);

    TEST_ASSERT(displayMgr.init().isOk(), "DisplayManager init");
    TEST_ASSERT(displayMgr.isDirty(), "Dirty after init");

    // First refresh should be full refresh
    displayMgr.refresh(true, 100);
    TEST_ASSERT(display.fullRefreshes_ == 1, "First refresh was full");
    TEST_ASSERT(!displayMgr.isDirty(), "Clean after refresh");

    // Subsequent partial refresh
    displayMgr.markDirty(false);
    TEST_ASSERT(displayMgr.isDirty(), "Dirty marked");
    displayMgr.refresh(false, 700);
    TEST_ASSERT(display.partialRefreshes_ == 1, "Partial refresh executed");
    TEST_ASSERT(displayMgr.getPartialFrameCount() == 1, "Partial frame count incremented");

    // Throttling: updateIfDirty fails if interval < 500ms
    displayMgr.markDirty(false);
    bool updatedEarly = displayMgr.updateIfDirty(800); // only 100ms since 700
    TEST_ASSERT(!updatedEarly, "Throttled: did not refresh within 100ms");

    // After 500ms elapsed:
    bool updatedOnTime = displayMgr.updateIfDirty(1300); // 600ms elapsed
    TEST_ASSERT(updatedOnTime, "Updated after min refresh interval");
    TEST_ASSERT(display.partialRefreshes_ == 2, "Second partial refresh count");

    for (uint16_t frame = 3; frame <= services::DisplayManager::FULL_REFRESH_FRAME_COUNT; ++frame) {
        displayMgr.refreshRect(ersa::Rect{0, 0, 20, 20}, false, 1300 + frame * 500);
    }
    TEST_ASSERT(displayMgr.getPartialFrameCount() == services::DisplayManager::FULL_REFRESH_FRAME_COUNT,
                "display manager tracks partial waveforms to its configured ghosting limit");
    displayMgr.refreshRect(ersa::Rect{0, 0, 20, 20}, false, 200000);
    TEST_ASSERT(display.fullRefreshes_ == 2 && displayMgr.getPartialFrameCount() == 0,
                "display manager schedules a full waveform after the partial frame budget");

    // Idle power off after 8000ms
    displayMgr.noteActivity(1300);
    displayMgr.tick(2000); // 700ms idle
    TEST_ASSERT(display.isPowered(), "Display still powered");

    displayMgr.tick(10000); // 8700ms idle
    TEST_ASSERT(!display.isPowered(), "Display powered off after idle timeout");

    TEST_PASS();
}

// -----------------------------------------------------------------------------
// Test NetworkManager & NetworkHandle RAII
// -----------------------------------------------------------------------------
void test_network_manager() {
    events::EventBus bus;
    test::MockWifi wifi;
    services::NetworkManager netMgr(bus, &wifi);
    services::NetworkManager::setInstance(&netMgr);

    TEST_ASSERT(netMgr.init().isOk(), "NetworkManager init");
    TEST_ASSERT(netMgr.getActiveHandleCount() == 0, "No active handles");

    {
        services::NetworkHandle handle1 = netMgr.requestInternet();
        TEST_ASSERT(handle1.isValid(), "Handle 1 valid");
        TEST_ASSERT(netMgr.getActiveHandleCount() == 1, "Active handles = 1");
        TEST_ASSERT(wifi.enableCalls == 1, "Network manager wakes radio through its HAL");

        {
            services::NetworkHandle handle2 = netMgr.requestInternet();
            TEST_ASSERT(netMgr.getActiveHandleCount() == 2, "Active handles = 2");
        }
        TEST_ASSERT(netMgr.getActiveHandleCount() == 1, "Handle 2 released on scope exit");
    }
    TEST_ASSERT(netMgr.getActiveHandleCount() == 0, "All network handles released via RAII");
    TEST_ASSERT(wifi.disconnectCalls == 1 && wifi.state() == hal::WifiState::Off,
                "Network manager powers down Wi-Fi through its HAL");

    TEST_PASS();
}

// -----------------------------------------------------------------------------
// Test StorageService
// -----------------------------------------------------------------------------
void test_storage_service() {
    auto& storage = services::StorageService::instance();
    TEST_ASSERT(storage.init().isOk(), "Storage init");

    storage.setString("wifi.ssid", "Ersa_Network");
    TEST_ASSERT(storage.getString("wifi.ssid") == "Ersa_Network", "String match");

    storage.setInt("display.brightness", 85);
    TEST_ASSERT(storage.getInt("display.brightness") == 85, "Int match");

    storage.setBool("power.saver", true);
    TEST_ASSERT(storage.getBool("power.saver") == true, "Bool match");

    const uint8_t record[] = {0x00, 0x7f, 0x80, 0xff};
    uint8_t restored[sizeof(record)]{};
    TEST_ASSERT(storage.setBytes("cache.blob", record, sizeof(record)), "Opaque cache bytes are stored");
    TEST_ASSERT(storage.getBytesLength("cache.blob") == sizeof(record), "Opaque cache record size is available");
    TEST_ASSERT(storage.getBytes("cache.blob", restored, sizeof(restored)) == sizeof(restored) &&
                memcmp(record, restored, sizeof(record)) == 0, "Opaque cache bytes round-trip exactly");

    storage.remove("display.brightness");
    TEST_ASSERT(storage.getInt("display.brightness", 50) == 50, "Default returned after removal");

    TEST_PASS();
}

// -----------------------------------------------------------------------------
// Test SettingsService & System Facade
// -----------------------------------------------------------------------------
void test_settings_service() {
    auto& settings = system::settings();
    settings.setWifiSsid("MyAccessPoint");
    TEST_ASSERT(settings.getWifiSsid() == "MyAccessPoint", "Ssid match");

    settings.setTimezoneOffsetMin(330);
    TEST_ASSERT(settings.getTimezoneOffsetMin() == 330, "Timezone match");

    settings.setMilitaryTime(true);
    TEST_ASSERT(settings.isMilitaryTime() == true, "Military time match");

    TEST_PASS();
}

// -----------------------------------------------------------------------------
// Test Complications
// -----------------------------------------------------------------------------
class BatteryComplication : public app::ComplicationProvider {
public:
    app::ComplicationData get() override {
        return app::ComplicationData{"85%"};
    }
};

void test_complications() {
    BatteryComplication bComp;
    app::ComplicationData data = bComp.get();
    TEST_ASSERT(data.text == "85%", "Battery complication text match");

    TEST_PASS();
}

// -----------------------------------------------------------------------------
// Test System Config Defaults & HTTP Date Parsing Fallback
// -----------------------------------------------------------------------------
#include "ersa/config/system_defaults.h"
#include "ersa/config/ui_strings.h"

static time_t testParseHttpDateToEpoch(const char* str) {
    if (!str || strlen(str) < 16) return 0;
    const char* p = strchr(str, ',');
    p = p ? (p + 1) : str;
    while (*p == ' ') p++;
    int day = atoi(p);
    if (day < 1 || day > 31) return 0;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    static const char* const months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    int month = 0;
    for (int m = 0; m < 12; ++m) {
        if (strncasecmp(p, months[m], 3) == 0) {
            month = m + 1;
            break;
        }
    }
    if (month == 0) return 0;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    int year = atoi(p);
    if (year < 2024 || year > 2099) return 0;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    int hour = atoi(p);
    p = strchr(p, ':');
    if (!p) return 0;
    int min = atoi(p + 1);
    p = strchr(p + 1, ':');
    if (!p) return 0;
    int sec = atoi(p + 1);

    // Basic days since 1970 calculation for host test verification
    int days = 0;
    for (int y = 1970; y < year; y++) {
        days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
    }
    static const int daysBeforeMonth[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    days += daysBeforeMonth[month - 1];
    if (month > 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
        days += 1;
    }
    days += (day - 1);
    return static_cast<time_t>(days * 86400LL + hour * 3600LL + min * 60LL + sec);
}

void test_config_and_fallbacks() {
    // 1. Validate NTP server pool fallback counts
    TEST_ASSERT(config::NUM_NTP_SERVERS >= 3, "At least 3 NTP fallback servers configured");
    TEST_ASSERT(strcmp(config::DEFAULT_NTP_SERVERS[0], "pool.ntp.org") == 0, "Primary NTP server is pool.ntp.org");
    TEST_ASSERT(strcmp(config::DEFAULT_NTP_SERVERS[1], "time.google.com") == 0, "Secondary NTP server is time.google.com");
    TEST_ASSERT(strcmp(config::DEFAULT_NTP_SERVERS[2], "time.cloudflare.com") == 0, "Tertiary NTP server is time.cloudflare.com");

    // 2. Validate HTTP Time endpoints fallback counts
    TEST_ASSERT(config::NUM_HTTP_TIME_ENDPOINTS >= 3, "At least 3 HTTP fallback endpoints configured");
    TEST_ASSERT(strstr(config::DEFAULT_HTTP_TIME_ENDPOINTS[0], "google.com") != nullptr, "Primary HTTP fallback is Google");

    // 3. Validate centralized strings
    TEST_ASSERT(strlen(strings::MSG_READY) > 0, "MSG_READY non-empty");
    TEST_ASSERT(strlen(strings::MSG_NTP_SYNCED) > 0, "MSG_NTP_SYNCED non-empty");
    TEST_ASSERT(strlen(strings::MSG_HTTP_TIME_SYNCED) > 0, "MSG_HTTP_TIME_SYNCED non-empty");
    TEST_ASSERT(strlen(strings::NAV_DRAWER_FOOTER) > 0, "NAV_DRAWER_FOOTER non-empty");

    // 4. Validate HTTP Date header parser
    const char* testHeader = "Mon, 28 Sep 2026 06:21:00 GMT";
    time_t epoch = testParseHttpDateToEpoch(testHeader);
    TEST_ASSERT(epoch > 1700000000, "Epoch should be in modern range (> 2023)");

    // Test without day of week
    const char* testHeaderNoDow = "28 Sep 2026 06:21:00 GMT";
    time_t epochNoDow = testParseHttpDateToEpoch(testHeaderNoDow);
    TEST_ASSERT(epoch == epochNoDow, "Header without day-of-week should match");

    TEST_PASS();
}

// -----------------------------------------------------------------------------
// Test BluetoothManager & Telephony / Media
// -----------------------------------------------------------------------------
void test_bluetooth_manager() {
    events::EventBus bus;
    test::MockBluetooth mockBle;
    test::MockBluetooth mockSource; // Provider is a separate object from the BLE transport.
    services::BluetoothManager bleMgr(mockBle, mockSource, bus);
    bleMgr.init();

    // 1. Check initial state
    TEST_ASSERT(!bleMgr.isConnected(), "Should start disconnected");
    TEST_ASSERT(mockBle.isAdvertising(), "Should start advertising on init");
    TEST_ASSERT(bleMgr.getCallState() == services::CallState::Idle, "Initial call state should be Idle");
    TEST_ASSERT(bleMgr.getRecentCallCount() == 0, "Should not seed fake dialable phone numbers");

    // 2. Track events from EventBus
    bool gotBleConnected = false;
    bool gotBleDisconnected = false;
    bool gotSourceDisconnected = false;
    bool gotCallIncoming = false;
    bool gotCallAccepted = false;
    bool gotCallEnded = false;
    char lastMediaTrackReceived[32] = "";

    bus.subscribe(events::EventType::BleConnected, [](const events::Event&, void* u) {
        *static_cast<bool*>(u) = true;
    }, &gotBleConnected);

    bus.subscribe(events::EventType::BleDisconnected, [](const events::Event&, void* u) {
        *static_cast<bool*>(u) = true;
    }, &gotBleDisconnected);
    bus.subscribe(events::EventType::CompanionDisconnected, [](const events::Event&, void* u) {
        *static_cast<bool*>(u) = true;
    }, &gotSourceDisconnected);

    bus.subscribe(events::EventType::CallIncoming, [](const events::Event&, void* u) {
        *static_cast<bool*>(u) = true;
    }, &gotCallIncoming);

    bus.subscribe(events::EventType::CallAccepted, [](const events::Event&, void* u) {
        *static_cast<bool*>(u) = true;
    }, &gotCallAccepted);

    bus.subscribe(events::EventType::CallEnded, [](const events::Event&, void* u) {
        *static_cast<bool*>(u) = true;
    }, &gotCallEnded);

    bus.subscribe(events::EventType::MediaTrackChanged, [](const events::Event& e, void* u) {
        auto* buf = static_cast<char*>(u);
        strncpy(buf, e.media.title, 31);
    }, lastMediaTrackReceived);

    // 3. Test Connection
    mockBle.simulateConnection(true);
    TEST_ASSERT(bleMgr.isConnected(), "Transport reports its link even before a provider is ready");
    TEST_ASSERT(!bleMgr.notificationsReady(), "Unavailable provider exposes no capabilities");
    TEST_ASSERT(!bleMgr.acceptCall(), "Unsupported or unavailable provider rejects call commands");
    mockSource.simulateConnection(true);
    TEST_ASSERT(bleMgr.isConnected(), "BluetoothManager should report connected");
    TEST_ASSERT(gotBleConnected, "EventBus should receive BleConnected");
    TEST_ASSERT(strcmp(bleMgr.companionSourceId(), "test-mock") == 0,
                "Manager exposes the selected source identity");

    // 4. Test Incoming Call
    mockSource.simulateIncomingCall("Alice", "+15551234");
    TEST_ASSERT(bleMgr.getCallState() == services::CallState::Incoming, "Call state should be Incoming");
    TEST_ASSERT(strcmp(bleMgr.getCallerName(), "Alice") == 0, "Caller name should be Alice");
    TEST_ASSERT(strcmp(bleMgr.getCallerNumber(), "+15551234") == 0, "Caller number should match");
    TEST_ASSERT(gotCallIncoming, "EventBus should receive CallIncoming");

    // Recents check - top recent should now be Alice
    TEST_ASSERT(strcmp(bleMgr.getRecentCall(0).name, "Alice") == 0, "Top recent should be Alice");
    TEST_ASSERT(services::StorageService::instance().getString("recent_num_0") == "+15551234",
                "Recent contacts should be persisted for the PBAP-ready recent list");

    // 5. Test Accept Call (B1 pressed)
    TEST_ASSERT(bleMgr.acceptCall(), "Provider confirms that answer command was accepted");
    TEST_ASSERT(bleMgr.getCallState() == services::CallState::Incoming, "Submission must not fabricate active call state");
    TEST_ASSERT(!gotCallAccepted, "No accepted event before phone confirmation");
    mockSource.simulateCallAnswered();
    TEST_ASSERT(bleMgr.getCallState() == services::CallState::Active, "Phone confirms active call");
    TEST_ASSERT(mockSource.acceptCount() == 1, "HAL acceptCall should be called once");
    TEST_ASSERT(gotCallAccepted, "EventBus should receive CallAccepted");

    // 6. Test Hang Up Call (B2 pressed)
    TEST_ASSERT(bleMgr.hangupCall(), "Provider confirms that hangup command was accepted");
    TEST_ASSERT(bleMgr.getCallState() == services::CallState::Active, "Hangup waits for phone confirmation");
    mockSource.simulateCallEnded();
    TEST_ASSERT(bleMgr.getCallState() == services::CallState::Idle, "Call end returns Calls to its recent list");
    TEST_ASSERT(mockSource.hangupCount() == 1, "HAL hangupCall should be called once");
    TEST_ASSERT(gotCallEnded, "EventBus should receive CallEnded");

    // 7. Test Quick Dial Recent (Hold B1)
    TEST_ASSERT(bleMgr.dialRecent(0), "Provider accepts dialing a saved recent"); // Dial Alice
    TEST_ASSERT(mockSource.dialCount() == 1, "HAL dial should be called once");
    TEST_ASSERT(strcmp(mockSource.lastDialed(), "+15551234") == 0, "Dialed number should match Alice's number");
    TEST_ASSERT(bleMgr.getCallState() == services::CallState::Active, "Call state should be Active after dial");

    bleMgr.hangupCall();

    mockSource.simulateIncomingCall("Name Only", "");
    TEST_ASSERT(bleMgr.getRecentCallCount() >= 2, "Name-only ANCS caller is kept in recent calls");
    TEST_ASSERT(strcmp(bleMgr.getRecentCall(0).name, "Name Only") == 0, "Name-only caller appears first in recents");
    TEST_ASSERT(bleMgr.getRecentCall(0).number[0] == '\0', "Name-only caller remains visibly non-dialable");
    TEST_ASSERT(services::StorageService::instance().getString("recent_name_0") == "Name Only",
                "Name-only recent caller is persisted");
    mockSource.simulateCallEnded();
    TEST_ASSERT(bleMgr.getCallState() == services::CallState::Idle, "Name-only call end returns to recents");

    mockSource.simulateIncomingCall("", "");
    TEST_ASSERT(strcmp(bleMgr.getRecentCall(0).name, "unknown caller") == 0,
                "Call is retained even if ANCS has not delivered caller attributes");
    const size_t countBeforeCallerUpdate = bleMgr.getRecentCallCount();
    mockSource.simulateIncomingCall("Bob", "+15559876");
    TEST_ASSERT(bleMgr.getRecentCallCount() == countBeforeCallerUpdate,
                "Late caller attributes enrich the pending recent instead of adding a duplicate");
    TEST_ASSERT(strcmp(bleMgr.getRecentCall(0).name, "Bob") == 0 &&
                strcmp(bleMgr.getRecentCall(0).number, "+15559876") == 0,
                "Late caller details replace the unknown recent entry");
    mockSource.simulateCallEnded();

    // 8. Test Media Control & Updates
    mockSource.simulateMedia(true, "Starboy", "The Weeknd");
    TEST_ASSERT(bleMgr.isPlaying(), "Media state should be playing");
    TEST_ASSERT(strcmp(bleMgr.getMediaTitle(), "Starboy") == 0, "Title should be Starboy");
    TEST_ASSERT(strcmp(bleMgr.getMediaArtist(), "The Weeknd") == 0, "Artist should be The Weeknd");
    TEST_ASSERT(strcmp(lastMediaTrackReceived, "Starboy") == 0, "EventBus should receive MediaTrackChanged");

    // Test B1 next track
    TEST_ASSERT(bleMgr.mediaNext(), "Provider accepts media command");
    TEST_ASSERT(mockSource.mediaCmdCount() == 1, "mediaCommand should be called once");
    TEST_ASSERT(mockSource.lastMediaAction() == hal::CompanionMediaAction::Next, "Action should be Next");

    // Test B2 play/pause toggle
    TEST_ASSERT(bleMgr.mediaToggle(), "Provider accepts media toggle");
    TEST_ASSERT(mockSource.mediaCmdCount() == 2, "mediaCommand should be called twice");
    TEST_ASSERT(mockSource.lastMediaAction() == hal::CompanionMediaAction::Toggle, "Action should be Toggle");
    TEST_ASSERT(bleMgr.isPlaying(), "Command submission must not fabricate paused state");
    mockSource.simulateMedia(false, "Starboy", "The Weeknd");
    TEST_ASSERT(!bleMgr.isPlaying(), "Phone confirms pause");

    // Test previous track
    bleMgr.mediaPrevious();
    TEST_ASSERT(mockSource.mediaCmdCount() == 3, "mediaCommand should be called 3 times");
    TEST_ASSERT(mockSource.lastMediaAction() == hal::CompanionMediaAction::Previous, "Action should be Previous");

    // 9. Test Notification
    bool gotNotification = false;
    char notifTitle[32] = "";
    char notifMsg[64] = "";
    struct NotifCapture {
        bool* got;
        char* title;
        char* msg;
    } cap{&gotNotification, notifTitle, notifMsg};

    bus.subscribe(events::EventType::NotificationReceived, [](const events::Event& e, void* u) {
        auto* c = static_cast<NotifCapture*>(u);
        *c->got = true;
        strncpy(c->title, e.notification.title, 31);
        strncpy(c->msg, e.notification.message, 63);
    }, &cap);

    mockSource.simulateNotification("WhatsApp", "Hey! Meeting starts in 5 mins", "WhatsApp", 42);
    TEST_ASSERT(gotNotification, "EventBus should receive NotificationReceived");
    TEST_ASSERT(strcmp(notifTitle, "WhatsApp") == 0, "Event title should match");
    TEST_ASSERT(strcmp(notifMsg, "Hey! Meeting starts in 5 mins") == 0, "Event message should match");
    TEST_ASSERT(bleMgr.getNotificationCount() == 1, "Should have 1 notification in history");
    TEST_ASSERT(strcmp(bleMgr.getNotification(0).title, "WhatsApp") == 0, "Notification title should match");
    TEST_ASSERT(strcmp(bleMgr.getNotification(0).message, "Hey! Meeting starts in 5 mins") == 0, "Notification message should match");

    mockSource.simulateNotification("Updated", "new body", "Mail", 42);
    TEST_ASSERT(bleMgr.getNotificationCount() == 1, "Modified UID replaces history entry");
    TEST_ASSERT(strcmp(bleMgr.getNotification(0).title, "Updated") == 0, "Updated title retained");

    mockSource.simulateNotification(nullptr, nullptr, "", 42);
    TEST_ASSERT(bleMgr.getNotificationCount() == 0, "Removed UID leaves notification history");
    mockSource.simulateNotification("New session data", "body", "Mail", 43);

    mockSource.simulateNotification(nullptr, nullptr, nullptr, 0);
    TEST_ASSERT(bleMgr.isConnected(), "ANCS subscription reset keeps BLE link connected");
    TEST_ASSERT(bleMgr.getNotificationCount() == 0, "ANCS reset clears old session history");
    mockSource.simulateNotification("Recovered", "body", "Mail", 44);
    TEST_ASSERT(bleMgr.getNotificationCount() == 1, "Notifications resume after subscription reset");

    mockSource.simulateNotification("Second", "second body", "Mail", 45, true);
    TEST_ASSERT(bleMgr.getNotificationCount() == 2, "Two notifications can be browsed");
    TEST_ASSERT(bleMgr.dismissNotification(0), "Selected notification can be dismissed");
    TEST_ASSERT(mockSource.notificationDismissCount() == 1 && mockSource.lastDismissedUid() == 45,
                "Advertised dismissal action is forwarded to the phone");
    TEST_ASSERT(bleMgr.getNotificationCount() == 1, "Dismiss removes only selected notification");
    TEST_ASSERT(bleMgr.getNotification(0).uid == 44, "Other notification remains visible");
    gotNotification = false;
    mockSource.simulateNotification("Second updated", "late body", "Mail", 45);
    TEST_ASSERT(bleMgr.getNotificationCount() == 1, "Late ANCS update cannot restore dismissed UID");
    TEST_ASSERT(!gotNotification, "Late dismissed update does not reopen notification screen");
    TEST_ASSERT(!bleMgr.dismissNotification(1), "Out-of-range dismissal is ignored");
    mockSource.simulateNotification(nullptr, nullptr, "", 45);
    mockSource.simulateNotification("New UID", "new body", "Mail", 45);
    TEST_ASSERT(bleMgr.getNotificationCount() == 2, "Removal releases UID suppression");
    mockSource.simulateNotification(nullptr, nullptr, nullptr, 0);
    TEST_ASSERT(bleMgr.getNotificationCount() == 0, "ANCS reset clears notification history");
    mockSource.simulateNotification("New session", "body", "Mail", 45);
    TEST_ASSERT(bleMgr.getNotificationCount() == 1, "New session accepts previously dismissed UID");

    // 10. Disconnect
    mockBle.simulateConnection(false);
    mockSource.simulateConnection(false);
    TEST_ASSERT(!bleMgr.isConnected(), "Should report disconnected");
    TEST_ASSERT(gotBleDisconnected, "EventBus should receive BleDisconnected");
    TEST_ASSERT(gotSourceDisconnected, "Provider availability loss is transport-neutral");
    TEST_ASSERT(bleMgr.getCallState() == services::CallState::Idle, "Disconnect clears stale call");
    TEST_ASSERT(!bleMgr.isPlaying(), "Disconnect clears media state");
    TEST_ASSERT(bleMgr.getNotificationCount() == 0, "Disconnect invalidates ANCS session UIDs");
    bleMgr.acceptCall();
    TEST_ASSERT(mockSource.acceptCount() == 1, "Cannot answer after disconnect");
    bleMgr.restartAdvertising();
    TEST_ASSERT(mockBle.isAdvertising(), "Manual reconnect advertises again");

    TEST_PASS();
}

void test_session_stats() {
    auto& storage = services::StorageService::instance();
    storage.clear();
    storage.setInt("sess_chk_s", 125);
    services::SessionStats::begin();
    TEST_ASSERT(services::SessionStats::previousSessionUptimeSeconds() == 125,
                "Last completed uptime loads from the previous checkpoint");
    services::SessionStats::tick(300000);
    services::SessionStats::begin();
    TEST_ASSERT(services::SessionStats::previousSessionUptimeSeconds() == 300,
                "A later boot reports the latest saved session uptime");
    TEST_PASS();
}

// -----------------------------------------------------------------------------
// Main Test Runner
// -----------------------------------------------------------------------------
void test_apple_protocols();

int main() {
    printf("==================================================\n");
    printf("        Ersa Smartwatch Core Architecture Tests   \n");
    printf("==================================================\n");

    test_apple_protocols();
    test_runtime_scheduler();
    test_console_hal_contract();
    test_display_hal_contract();
    test_power_management_hal_contract();
    test_event_bus();
    test_application_manager();
    test_time_service();
    test_power_manager();
    test_display_manager();
    test_network_manager();
    test_storage_service();
    test_settings_service();
    test_complications();
    test_config_and_fallbacks();
    test_bluetooth_manager();
    test_session_stats();

    printf("\nAll %d test suites passed successfully!\n", testsPassed);
    return 0;
}
