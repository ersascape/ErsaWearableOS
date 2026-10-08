#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ersa::board { class Pins; }

namespace ersa::hal {

/** Workload class used to acquire temporary performance guarantees. */
enum class PerformanceProfile : uint8_t { Interactive, Compute };

/**
 * Platform contract for clock scaling, wake sources, and low-power states.
 *
 * Power policy decides when automatic sleep is appropriate; this interface
 * configures the chip/RTOS and coordinates peripheral locks. The normal idle
 * path waits on the RTOS so automatic sleep can honor BLE and PM constraints.
 */
class IPowerManagement {
public:
    virtual ~IPowerManagement() = default;
    /** Configure PM/tickless-idle policy before radio stacks acquire locks. */
    virtual bool initialize() = 0;
    /** Sample or apply deferred PM work without blocking the application task. */
    virtual void tick() = 0;
    /// Configure GPIO/button wake sources from the selected board's pin map.
    virtual bool initializeWakeSources(const board::Pins& pins) = 0;
    /**
     * Permit FreeRTOS automatic sleep when true; false holds the platform's
     * no-light-sleep guard for display, network, USB, or active interaction.
     */
    virtual void allowAutomaticSleep(bool allow) = 0;
    /** Block the UI task until notifyWake() or timeout to reduce active polling. */
    virtual void waitForWake(uint32_t timeoutMs) = 0;
    /** Notify the task that new event/driver work is ready to process. */
    virtual void notifyWake() = 0;
    /**
     * Enter deep sleep after configuring enabled GPIO/timer sources.
     * This normally restarts firmware on wake, unlike light sleep.
     */
    virtual void enterDeepSleep(uint64_t timerUs) = 0;
    /**
     * Acquire a temporary CPU/APB minimum for latency-sensitive work.
     * @param profile Workload class selecting the platform's performance guarantee.
     * @param reason Short diagnostic label paired with releasePerformance().
     * @return False when the profile is unsupported or the platform lock failed.
     */
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

/** RAII lease that balances one performance lock on all scope exit paths. */
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
