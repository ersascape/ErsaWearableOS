#include "ersa/services/power_manager.h"
#include <string.h>

#if defined(ARDUINO) && defined(CONFIG_IDF_TARGET_ESP32C3)
#include <Arduino.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "ersa/board/board.h"
#include "core/debug_log.h"
#endif

namespace ersa {
namespace services {

static PowerManager* s_powerManagerInstance = nullptr;

WakeLock::WakeLock(const char* tag)
    : tag_(tag), active_(false) {
    if (s_powerManagerInstance && tag_) {
        active_ = s_powerManagerInstance->acquireWakeLockRaw(tag_);
    }
}

WakeLock::~WakeLock() {
    release();
}

WakeLock::WakeLock(WakeLock&& other) noexcept
    : tag_(other.tag_), active_(other.active_) {
    other.tag_ = nullptr;
    other.active_ = false;
}

WakeLock& WakeLock::operator=(WakeLock&& other) noexcept {
    if (this != &other) {
        release();
        tag_ = other.tag_;
        active_ = other.active_;
        other.tag_ = nullptr;
        other.active_ = false;
    }
    return *this;
}

void WakeLock::release() {
    if (active_ && s_powerManagerInstance && tag_) {
        s_powerManagerInstance->releaseWakeLockRaw(tag_);
        active_ = false;
        tag_ = nullptr;
    }
}

PowerManager& PowerManager::instance() {
    return *s_powerManagerInstance;
}

void PowerManager::setInstance(PowerManager* instance) {
    s_powerManagerInstance = instance;
}

PowerManager::PowerManager(hal::IBattery& battery, events::EventBus& bus)
    : battery_(battery), bus_(bus) {
    for (size_t i = 0; i < MAX_WAKE_LOCKS; ++i) {
        wakeLockTags_[i] = nullptr;
    }
}

Result<void> PowerManager::init() {
    Result<void> res = battery_.init();
    battery_.sample();
    lastBatterySampleMs_ = 0;
    cachedMv_ = battery_.millivolts();
    cachedPercent_ = battery_.percentage();
    cachedConnected_ = battery_.isConnected();
    cachedCharging_ = battery_.isCharging();
    return res;
}

WakeLock PowerManager::acquireWakeLock(const char* tag) {
    return WakeLock(tag);
}

bool PowerManager::acquireWakeLockRaw(const char* tag) {
    if (!tag) return false;
    for (size_t i = 0; i < MAX_WAKE_LOCKS; ++i) {
        if (wakeLockTags_[i] && strcmp(wakeLockTags_[i], tag) == 0) {
            return true; // Already acquired
        }
    }
    for (size_t i = 0; i < MAX_WAKE_LOCKS; ++i) {
        if (!wakeLockTags_[i]) {
            wakeLockTags_[i] = tag;
            activeWakeLocks_++;
            return true;
        }
    }
    return false;
}

bool PowerManager::releaseWakeLockRaw(const char* tag) {
    if (!tag) return false;
    for (size_t i = 0; i < MAX_WAKE_LOCKS; ++i) {
        if (wakeLockTags_[i] && strcmp(wakeLockTags_[i], tag) == 0) {
            wakeLockTags_[i] = nullptr;
            if (activeWakeLocks_ > 0) activeWakeLocks_--;
            return true;
        }
    }
    return false;
}

size_t PowerManager::getActiveWakeLockCount() const {
    return activeWakeLocks_;
}

bool PowerManager::hasWakeLocks() const {
    return activeWakeLocks_ > 0;
}

void PowerManager::noteActivity(uint32_t currentUptimeMs) {
    lastActivityMs_ = currentUptimeMs;
    state_ = PowerState::Active;
}

uint32_t PowerManager::getIdleTimeMs(uint32_t currentUptimeMs) const {
    if (currentUptimeMs >= lastActivityMs_) {
        return currentUptimeMs - lastActivityMs_;
    }
    return 0;
}

void PowerManager::tick(uint32_t currentUptimeMs) {
    if (nextBatterySampleDelayMs(currentUptimeMs) == 0) {
        lastBatterySampleMs_ = currentUptimeMs;
        battery_.sample();

        const uint16_t mv = battery_.millivolts();
        const uint8_t pct = battery_.percentage();
        const bool conn = battery_.isConnected();
        const bool chg = battery_.isCharging();

        if (conn) {
            const bool critical = (mv <= CRITICAL_BATTERY_MV || pct <= CRITICAL_BATTERY_PERCENT);
            if (critical) {
                if (criticalSampleCount_ < 3) ++criticalSampleCount_;
                if (criticalSampleCount_ >= 3) {
                    batteryPowerLevel_ = BatteryPowerLevel::Critical;
                }
            } else {
                criticalSampleCount_ = 0;
                if (batteryPowerLevel_ == BatteryPowerLevel::Critical &&
                    mv < 3550 && pct < 8) {
                    // Keep critical latched until the cell has clearly recovered.
                } else if (mv <= LOW_BATTERY_MV || pct <= LOW_BATTERY_PERCENT) {
                    batteryPowerLevel_ = BatteryPowerLevel::Low;
                } else if (mv >= 3700 && pct >= 24) {
                    batteryPowerLevel_ = BatteryPowerLevel::Normal;
                }
            }
        } else {
            criticalSampleCount_ = 0;
            batteryPowerLevel_ = BatteryPowerLevel::Normal;
        }

        if (mv != cachedMv_ || pct != cachedPercent_ || conn != cachedConnected_ || chg != cachedCharging_) {
            cachedMv_ = mv;
            cachedPercent_ = pct;
            cachedConnected_ = conn;
            cachedCharging_ = chg;

            bus_.publish(events::Event::createBatteryChanged(mv, pct, conn, chg, currentUptimeMs));
        }
    }
}

uint32_t PowerManager::batterySampleIntervalMs() const {
    // ADC voltage is only a rough state-of-charge estimate. Sample frequently
    // near low/critical thresholds, but avoid waking the CPU every 10 seconds
    // when the cell is comfortably charged.
    if (!cachedConnected_) return 60000;
    if (batteryPowerLevel_ == BatteryPowerLevel::Critical ||
        cachedMv_ <= CRITICAL_BATTERY_MV + 150 ||
        cachedPercent_ <= LOW_BATTERY_PERCENT) return 10000;
    if (batteryPowerLevel_ == BatteryPowerLevel::Low ||
        cachedMv_ <= 3800 || cachedPercent_ <= 35) return 20000;
    if (cachedMv_ >= 3900 && cachedPercent_ >= 70) return 60000;
    return 30000;
}

uint32_t PowerManager::nextBatterySampleDelayMs(uint32_t currentUptimeMs) const {
    const uint32_t interval = batterySampleIntervalMs();
    const uint32_t elapsed = currentUptimeMs - lastBatterySampleMs_;
    return elapsed >= interval ? 0 : interval - elapsed;
}

uint16_t PowerManager::getBatteryMv() const {
    return cachedMv_;
}

uint8_t PowerManager::getBatteryPercent() const {
    return cachedPercent_;
}

bool PowerManager::isBatteryConnected() const {
    return cachedConnected_;
}

bool PowerManager::isCharging() const {
    return cachedCharging_;
}

BatteryPowerLevel PowerManager::getBatteryPowerLevel() const {
    return batteryPowerLevel_;
}

PowerState PowerManager::getState() const {
    return state_;
}

void PowerManager::requestState(PowerState state) {
    state_ = state;
}

bool PowerManager::canSleep() const {
    if (hasWakeLocks()) return false;
    return true;
}

void PowerManager::enterLightSleep(uint64_t sleepTimeUs) {
#if defined(ARDUINO) && defined(CONFIG_IDF_TARGET_ESP32C3)
    state_ = PowerState::LightSleep;

    // Wake on the board's active-low buttons.
    gpio_wakeup_enable(static_cast<gpio_num_t>(ersa::board::Board::current().getPins().buttons().top.number), GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(static_cast<gpio_num_t>(ersa::board::Board::current().getPins().buttons().bottom.number), GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    if (sleepTimeUs > 0) {
        esp_sleep_enable_timer_wakeup(sleepTimeUs);
    }

    DebugLog::log("PWR: entering light sleep (max %llu s)", (unsigned long long)(sleepTimeUs / 1000000ULL));
    DebugLog::flush();

    esp_light_sleep_start();

    // CPU execution resumes directly after wakeup
    state_ = PowerState::Active;
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_GPIO) {
        DebugLog::log("PWR: woke from light sleep by GPIO");
        noteActivity(millis());
    } else if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        DebugLog::log("PWR: woke from light sleep by TIMER");
    } else {
        DebugLog::log("PWR: woke from light sleep cause=%d", int(cause));
    }
#else
    state_ = PowerState::LightSleep;
    (void)sleepTimeUs;
#endif
}

void PowerManager::enterDeepSleep(uint64_t sleepTimeUs) {
#if defined(ARDUINO) && defined(CONFIG_IDF_TARGET_ESP32C3)
    state_ = PowerState::DeepSleep;

    DebugLog::log("PWR: entering deep sleep...");
    DebugLog::flush();

    // Enable deep sleep wakeup on the board's active-low buttons.
    const uint64_t pinMask = (1ULL << ersa::board::Board::current().getPins().buttons().top.number) | (1ULL << ersa::board::Board::current().getPins().buttons().bottom.number);
    esp_deep_sleep_enable_gpio_wakeup(pinMask, ESP_GPIO_WAKEUP_GPIO_LOW);

    if (sleepTimeUs > 0) {
        esp_sleep_enable_timer_wakeup(sleepTimeUs);
    }

    esp_deep_sleep_start();
#else
    state_ = PowerState::DeepSleep;
    (void)sleepTimeUs;
#endif
}

} // namespace services
} // namespace ersa
