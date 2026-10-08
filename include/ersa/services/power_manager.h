#pragma once

#include "ersa/common/types.h"
#include "ersa/hal/battery.h"
#include "ersa/events/event_bus.h"
#include <stdint.h>
#include <stddef.h>

namespace ersa {
namespace services {

/** Coarse service state; the board power HAL performs physical sleep entry. */
enum class PowerState : uint8_t {
    Active = 0,
    Idle,
    LightSleep,
    DeepSleep
};

/** Hysteresis-filtered severity band used by battery and sleep policy. */
enum class BatteryPowerLevel : uint8_t {
    Normal = 0,
    Low,
    Critical
};

class PowerManager;

/**
 * RAII lease that prevents automatic sleep while a feature is active.
 * The tag is diagnostic and must remain valid until release. Scope-based use
 * makes cleanup reliable on early returns; move operations transfer the lease
 * without incrementing the manager's active-lock count.
 */
class WakeLock {
public:
    /** Acquire one sleep-prevention lease for a feature or operation. */
    explicit WakeLock(const char* tag);
    /** Release the lease during destruction; repeated explicit release is safe. */
    ~WakeLock();

    WakeLock(const WakeLock&) = delete;
    WakeLock& operator=(const WakeLock&) = delete;

    /** Transfer lease ownership; the moved-from object becomes inactive. */
    WakeLock(WakeLock&& other) noexcept;
    /** Release the current lease, then transfer the source object's lease. */
    WakeLock& operator=(WakeLock&& other) noexcept;

    /** Release before scope exit when work completes early. */
    void release();

private:
    const char* tag_{nullptr};
    bool active_{false};
};

/**
 * Applies battery qualification, wake-lock accounting, and sleep eligibility.
 * Product-level policy is kept here while IPowerManagement owns clocks, GPIO
 * wake sources, and chip sleep APIs. This separation makes thresholds and
 * activity decisions host-testable and keeps app code from bypassing radio
 * coordination in the platform layer.
 */
class PowerManager {
public:
    /** Maximum active lock tags tracked without heap allocation. */
    static constexpr size_t MAX_WAKE_LOCKS = 16;
    /** Idle interval relevant to legacy service-level deep-sleep transitions. */
    static constexpr uint32_t DEEP_SLEEP_TIMEOUT_MS = 60000;
    /** Percentage threshold used to enter the low battery band. */
    static constexpr uint8_t LOW_BATTERY_PERCENT = 20;
    /** Voltage threshold used to enter the low battery band. */
    static constexpr uint16_t LOW_BATTERY_MV = 3600;
    /** Percentage threshold used to enter the critical battery band. */
    static constexpr uint8_t CRITICAL_BATTERY_PERCENT = 3;
    /** Voltage threshold used to enter the critical battery band. */
    static constexpr uint16_t CRITICAL_BATTERY_MV = 3350;

    /** Bind the battery HAL and event bus; neither is owned by the manager. */
    explicit PowerManager(hal::IBattery& battery, events::EventBus& bus = events::EventBus::instance());

    /** Sample once, seed cached telemetry, and establish initial severity. */
    Result<void> init();
    /**
     * Sample when due, apply hysteresis/critical qualification, and publish
     * state transitions. The explicit uptime makes interval behavior testable
     * without a hardware clock.
     */
    void tick(uint32_t currentUptimeMs);
    /** Return the sample deadline so the UI can sleep until useful work is due. */
    uint32_t nextBatterySampleDelayMs(uint32_t currentUptimeMs) const;

    // WakeLock API (Section 10)
    /** Prefer this scoped lease when the protected operation has lexical lifetime. */
    WakeLock acquireWakeLock(const char* tag);
    /** Manually acquire a tagged lease for state spanning multiple callbacks. */
    bool acquireWakeLockRaw(const char* tag);
    /** Release one matching manual lease; false means the tag was not active. */
    bool releaseWakeLockRaw(const char* tag);
    /// Return the number of active wake locks.
    size_t getActiveWakeLockCount() const;
    /// Return whether at least one wake lock blocks sleep.
    bool hasWakeLocks() const;

    /** Reset the idle reference point after interaction or foreground work. */
    void noteActivity(uint32_t currentUptimeMs);
    /** Compute wrap-safe idle duration from the last recorded activity. */
    uint32_t getIdleTimeMs(uint32_t currentUptimeMs) const;

    /// Return the latest battery voltage in millivolts.
    uint16_t getBatteryMv() const;
    /// Return the age of the most recent battery sample in milliseconds.
    uint32_t getBatterySampleAgeMs(uint32_t nowMs) const { return nowMs - lastBatterySampleMs_; }
    /// Return whether at least one battery sample has been recorded.
    bool hasBatterySample() const { return cachedMv_ != 0; }
    /// Return estimated battery charge as a percentage.
    uint8_t getBatteryPercent() const;
    /// Return whether a usable battery is connected.
    bool isBatteryConnected() const;
    /// Return whether external charging power is present.
    bool isCharging() const;
    /** Return the filtered battery band used by user alerts and sleep policy. */
    BatteryPowerLevel getBatteryPowerLevel() const;

    /// Return the current service-level power state.
    PowerState getState() const;
    /// Set the service-level power state.
    void requestState(PowerState state);

    /** True only when no active feature lease blocks automatic sleep. */
    bool canSleep() const;
    /** Delegate deep sleep; zero timer leaves configured GPIO wakes as sources. */
    void enterDeepSleep(uint64_t sleepTimeUs = 0);

    /// Return the installed process-wide power manager.
    static PowerManager& instance();
    /// Install the process-wide power manager.
    static void setInstance(PowerManager* instance);

private:
    hal::IBattery& battery_;
    events::EventBus& bus_;

    PowerState state_{PowerState::Active};
    uint32_t lastActivityMs_{0};
    uint32_t lastBatterySampleMs_{0};
    uint32_t batterySampleIntervalMs() const;

    uint16_t cachedMv_{0};
    uint8_t cachedPercent_{100};
    bool cachedConnected_{false};
    bool cachedCharging_{false};
    BatteryPowerLevel batteryPowerLevel_{BatteryPowerLevel::Normal};
    uint8_t criticalSampleCount_{0};

    const char* wakeLockTags_[MAX_WAKE_LOCKS];
    uint16_t wakeLockReferences_[MAX_WAKE_LOCKS];
    size_t activeWakeLocks_{0};
};

} // namespace services
} // namespace ersa
