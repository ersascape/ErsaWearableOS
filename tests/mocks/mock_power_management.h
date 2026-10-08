#pragma once

#include "ersa/hal/power_management.h"

namespace ersa::test {
class MockPowerManagement final : public hal::IPowerManagement {
public:
    bool initialize() override { return true; }
    void tick() override {}
    bool initializeWakeSources(const board::Pins&) override { wakeSourcesInitialized = true; return true; }
    void allowAutomaticSleep(bool allow) override { sleepAllowed = allow; }
    void waitForWake(uint32_t timeoutMs) override { lastWaitMs = timeoutMs; }
    void notifyWake() override { ++wakeNotifications; }
    void enterDeepSleep(uint64_t timerUs) override { lastDeepSleepUs = timerUs; }
    bool acquirePerformance(hal::PerformanceProfile, const char*) override { ++performanceAcquires; return true; }
    void releasePerformance(hal::PerformanceProfile, const char*) override { ++performanceReleases; }
    void reportPowerModes() override {}
    bool getPowerModeReport(char*, size_t) override { return false; }
    bool setTestCpuFrequencyMHz(unsigned mhz) override { testMHz = mhz; return true; }
    unsigned testCpuFrequencyMHz() const override { return testMHz; }

    bool wakeSourcesInitialized{false};
    bool sleepAllowed{false};
    uint32_t lastWaitMs{0};
    uint32_t wakeNotifications{0};
    uint64_t lastDeepSleepUs{0};
    uint32_t performanceAcquires{0};
    uint32_t performanceReleases{0};
    unsigned testMHz{0};
};
} // namespace ersa::test
