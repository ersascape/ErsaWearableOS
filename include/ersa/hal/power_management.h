#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ersa::board { class Pins; }

namespace ersa::hal {

/// Workload class used to acquire temporary performance guarantees.
enum class PerformanceProfile : uint8_t { Interactive, Compute };

/// Platform contract for clock scaling, sleep policy, wake sources, and sleep entry.
class IPowerManagement {
public:
    virtual ~IPowerManagement() = default;
    /// Configure the platform power policy before radios and peripherals start.
    virtual bool initialize() = 0;
    /// Run periodic platform power-management maintenance.
    virtual void tick() = 0;
    /// Configure GPIO/button wake sources from the selected board's pin map.
    virtual bool initializeWakeSources(const board::Pins& pins) = 0;
    /// Allow or block automatic idle sleep.
    virtual void allowAutomaticSleep(bool allow) = 0;
    /// Wait for a task wake notification or a timeout in milliseconds.
    virtual void waitForWake(uint32_t timeoutMs) = 0;
    /// Wake a task blocked in waitForWake.
    virtual void notifyWake() = 0;
    /// Enter light sleep, optionally with a timer wake source in microseconds.
    virtual void enterLightSleep(uint64_t timerUs) = 0;
    /// Enter deep sleep, optionally with a timer wake source in microseconds.
    virtual void enterDeepSleep(uint64_t timerUs) = 0;
    /// Acquire a performance profile; return false when unsupported or unavailable.
    virtual bool acquirePerformance(PerformanceProfile profile, const char* reason) = 0;
    /// Release a performance profile acquired earlier.
    virtual void releasePerformance(PerformanceProfile profile, const char* reason) = 0;
    /// Emit a one-time power-mode diagnostic report.
    virtual void reportPowerModes() = 0;
    /// Copy a power-mode report into a caller-owned buffer.
    virtual bool getPowerModeReport(char* buffer, size_t capacity) = 0;
    /// Queue a development CPU-frequency override for the next controlled restart.
    virtual bool setTestCpuFrequencyMHz(unsigned mhz) = 0;
    /// Return the queued development CPU-frequency override, or zero for automatic mode.
    virtual unsigned testCpuFrequencyMHz() const = 0;
};

/// RAII lease for a temporary workload performance profile.
class PerformanceScope {
public:
    /// Acquire a workload profile and release it when this scope ends.
    PerformanceScope(IPowerManagement& power, PerformanceProfile profile, const char* reason)
        : power_(&power), profile_(profile), reason_(reason), acquired_(power.acquirePerformance(profile, reason)) {}
    /// Release the performance profile if it was successfully acquired.
    ~PerformanceScope() { if (acquired_) power_->releasePerformance(profile_, reason_); }
    PerformanceScope(const PerformanceScope&) = delete;
    PerformanceScope& operator=(const PerformanceScope&) = delete;
private:
    IPowerManagement* power_;
    PerformanceProfile profile_;
    const char* reason_;
    bool acquired_;
};

} // namespace ersa::hal
