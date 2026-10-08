#pragma once

#include "ersa/hal/power_management.h"
#if defined(ARDUINO)
#include <esp_pm.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace ersa::hal {

class Esp32PowerManagement final : public IPowerManagement {
public:
    bool initialize() override;
    void tick() override;
    bool initializeWakeSources(const board::Pins& pins) override;
    void allowAutomaticSleep(bool allow) override;
    void waitForWake(uint32_t timeoutMs) override;
    void notifyWake() override;
    void enterLightSleep(uint64_t timerUs) override;
    void enterDeepSleep(uint64_t timerUs) override;
    bool acquirePerformance(PerformanceProfile profile, const char* reason) override;
    void releasePerformance(PerformanceProfile profile, const char* reason) override;
    void reportPowerModes() override;
    bool getPowerModeReport(char* buffer, size_t capacity) override;
    bool setTestCpuFrequencyMHz(unsigned mhz) override;
    unsigned testCpuFrequencyMHz() const override;

private:
    static void buttonIsr();
    static Esp32PowerManagement* active_;
#if defined(ARDUINO)
    TaskHandle_t uiTask_{nullptr};
    esp_pm_lock_handle_t noSleepLock_{nullptr};
#else
    void* uiTask_{nullptr};
    void* noSleepLock_{nullptr};
#endif
    int topButtonPin_{-1};
    int bottomButtonPin_{-1};
    bool initialized_{false};
    bool noSleepLockHeld_{true};
};

} // namespace ersa::hal
