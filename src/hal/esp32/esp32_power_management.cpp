#include "hal/esp32/esp32_power_management.h"
#include "ersa/board/pins.h"
#include "hal/esp32/esp32_dvfs_backend.h"
#include "core/debug_log.h"
#if defined(ARDUINO)
#include <Arduino.h>
#include <sdkconfig.h>
#if defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE && defined(CONFIG_FREERTOS_USE_TICKLESS_IDLE) && CONFIG_FREERTOS_USE_TICKLESS_IDLE
#include <esp_sleep.h>
#include <driver/gpio.h>
#endif
#endif

namespace ersa::hal {
Esp32PowerManagement* Esp32PowerManagement::active_ = nullptr;

bool Esp32PowerManagement::initialize() {
#if defined(ARDUINO) && defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE && defined(CONFIG_FREERTOS_USE_TICKLESS_IDLE) && CONFIG_FREERTOS_USE_TICKLESS_IDLE
    const unsigned profile = Dvfs::testCpuFrequencyMHz();
    const int maxMHz = profile == 80 ? 80 : profile == 40 ? 80 : profile == 160 ? 160 : 160;
    const int minMHz = profile == 80 ? 80 : profile == 160 ? 160 : 40;
    const esp_pm_config_esp32c3_t config{.max_freq_mhz = maxMHz, .min_freq_mhz = minMHz, .light_sleep_enable = true};
    const esp_err_t result = esp_pm_configure(&config);
    DebugLog::log("PWR: CPU profile=%u MHz (%s) light sleep status=0x%x", profile, profile ? "test" : "automatic", unsigned(result));
    initialized_ = result == ESP_OK && Dvfs::begin();
    return initialized_;
#else
    DebugLog::log("PWR: automatic light sleep unavailable (PM/tickless-idle config missing)");
    return false;
#endif
}

void Esp32PowerManagement::tick() { Dvfs::tick(); }

bool Esp32PowerManagement::initializeWakeSources(const board::Pins& pins) {
#if defined(ARDUINO) && defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE && defined(CONFIG_FREERTOS_USE_TICKLESS_IDLE) && CONFIG_FREERTOS_USE_TICKLESS_IDLE
    active_ = this;
    topButtonPin_ = pins.buttons().top.number;
    bottomButtonPin_ = pins.buttons().bottom.number;
    uiTask_ = xTaskGetCurrentTaskHandle();
    if (esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "ui-active", &noSleepLock_) != ESP_OK ||
        esp_pm_lock_acquire(noSleepLock_) != ESP_OK) return false;
    noSleepLockHeld_ = true;
    const auto& buttons = pins.buttons();
    gpio_wakeup_enable(static_cast<gpio_num_t>(buttons.top.number), GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(static_cast<gpio_num_t>(buttons.bottom.number), GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    attachInterrupt(digitalPinToInterrupt(buttons.top.number), buttonIsr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(buttons.bottom.number), buttonIsr, CHANGE);
    return true;
#else
    (void)pins;
    return false;
#endif
}

void Esp32PowerManagement::buttonIsr() {
#if defined(ARDUINO)
    auto* self = active_;
    if (!self || !self->uiTask_) return;
    BaseType_t higher = pdFALSE;
    vTaskNotifyGiveFromISR(self->uiTask_, &higher);
    if (higher) portYIELD_FROM_ISR();
#endif
}
void Esp32PowerManagement::allowAutomaticSleep(bool allow) {
#if defined(ARDUINO) && defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE && defined(CONFIG_FREERTOS_USE_TICKLESS_IDLE) && CONFIG_FREERTOS_USE_TICKLESS_IDLE
    if (!noSleepLock_) return;
    if (allow && noSleepLockHeld_) {
        esp_pm_lock_release(noSleepLock_);
        noSleepLockHeld_ = false;
    } else if (!allow && !noSleepLockHeld_) {
        esp_pm_lock_acquire(noSleepLock_);
        noSleepLockHeld_ = true;
    }
#else
    (void)allow;
#endif
}
void Esp32PowerManagement::waitForWake(uint32_t timeoutMs) {
#if defined(ARDUINO)
    TickType_t ticks = pdMS_TO_TICKS(timeoutMs);
    if (!ticks) ticks = 1;
    ulTaskNotifyTake(pdTRUE, ticks);
#else
    (void)timeoutMs;
#endif
}
void Esp32PowerManagement::notifyWake() {
#if defined(ARDUINO)
    if (uiTask_) xTaskNotifyGive(uiTask_);
#endif
}
void Esp32PowerManagement::enterDeepSleep(uint64_t timerUs) {
#if defined(ARDUINO) && defined(CONFIG_IDF_TARGET_ESP32C3) && defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE && defined(CONFIG_FREERTOS_USE_TICKLESS_IDLE) && CONFIG_FREERTOS_USE_TICKLESS_IDLE
    if (timerUs) esp_sleep_enable_timer_wakeup(timerUs);
    const uint64_t mask = (1ULL << topButtonPin_) | (1ULL << bottomButtonPin_);
    esp_deep_sleep_enable_gpio_wakeup(mask, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
#else
    (void)timerUs;
#endif
}
bool Esp32PowerManagement::acquirePerformance(PerformanceProfile profile, const char* reason) {
    return Dvfs::acquire(profile == PerformanceProfile::Compute ? Dvfs::Profile::Compute : Dvfs::Profile::Interactive, reason);
}
void Esp32PowerManagement::releasePerformance(PerformanceProfile profile, const char* reason) {
    Dvfs::release(profile == PerformanceProfile::Compute ? Dvfs::Profile::Compute : Dvfs::Profile::Interactive, reason);
}
void Esp32PowerManagement::reportPowerModes() { Dvfs::reportPowerModes(); }
bool Esp32PowerManagement::getPowerModeReport(char* b, size_t n) { return Dvfs::getPowerModeReport(b, n); }
bool Esp32PowerManagement::setTestCpuFrequencyMHz(unsigned mhz) { return Dvfs::setTestCpuFrequencyMHz(mhz); }
unsigned Esp32PowerManagement::testCpuFrequencyMHz() const { return Dvfs::testCpuFrequencyMHz(); }
} // namespace ersa::hal
