#pragma once

#include "ersa/common/types.h"
#include "ersa/hal/battery.h"
#include "ersa/events/event_bus.h"
#include <stdint.h>
#include <stddef.h>

namespace ersa {
namespace services {

enum class PowerState : uint8_t {
    Active = 0,
    Idle,
    LightSleep,
    DeepSleep
};

enum class BatteryPowerLevel : uint8_t {
    Normal = 0,
    Low,
    Critical
};

class PowerManager;

/// RAII lease that prevents the system power policy from sleeping.
class WakeLock {
public:
    /// Acquire a wake lock associated with a diagnostic tag.
    explicit WakeLock(const char* tag);
    /// Release this wake lock if it remains active.
    ~WakeLock();

    WakeLock(const WakeLock&) = delete;
    WakeLock& operator=(const WakeLock&) = delete;

    /// Move a wake-lock lease without changing the active lease count.
    WakeLock(WakeLock&& other) noexcept;
    /// Release this lease, then take ownership of another lease.
    WakeLock& operator=(WakeLock&& other) noexcept;

    /// Release the wake lock early.
    void release();

private:
    const char* tag_{nullptr};
    bool active_{false};
};

/// Applies battery sampling, wake-lock, and service-level power policy.
class PowerManager {
public:
    static constexpr size_t MAX_WAKE_LOCKS = 16;
    static constexpr uint32_t DEEP_SLEEP_TIMEOUT_MS = 60000;
    static constexpr uint8_t LOW_BATTERY_PERCENT = 20;
    static constexpr uint16_t LOW_BATTERY_MV = 3600;
    static constexpr uint8_t CRITICAL_BATTERY_PERCENT = 3;
    static constexpr uint16_t CRITICAL_BATTERY_MV = 3350;

    /// Manage battery telemetry and power events from the supplied sources.
    explicit PowerManager(hal::IBattery& battery, events::EventBus& bus = events::EventBus::instance());

    /// Initialize battery sampling and publish the initial power state.
    Result<void> init();
    /// Update battery state and publish threshold transitions.
    void tick(uint32_t currentUptimeMs);
    /// Return milliseconds until the next battery sample is due.
    uint32_t nextBatterySampleDelayMs(uint32_t currentUptimeMs) const;

    // WakeLock API (Section 10)
    /// Acquire an RAII wake lock.
    WakeLock acquireWakeLock(const char* tag);
    /// Acquire a wake lock by tag for call sites that manage lifetime manually.
    bool acquireWakeLockRaw(const char* tag);
    /// Release one wake lock associated with the supplied tag.
    bool releaseWakeLockRaw(const char* tag);
    /// Return the number of active wake locks.
    size_t getActiveWakeLockCount() const;
    /// Return whether at least one wake lock blocks sleep.
    bool hasWakeLocks() const;

    /// Record user or service activity at the given uptime.
    void noteActivity(uint32_t currentUptimeMs);
    /// Return milliseconds since the most recent activity.
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
    /// Return the current battery severity band.
    BatteryPowerLevel getBatteryPowerLevel() const;

    /// Return the current service-level power state.
    PowerState getState() const;
    /// Set the service-level power state.
    void requestState(PowerState state);

    /// Return whether wake locks currently allow the system to sleep.
    bool canSleep() const;
    /// Delegate light-sleep entry to the board power HAL.
    void enterLightSleep(uint64_t sleepTimeUs);
    /// Delegate deep-sleep entry to the board power HAL.
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
    size_t activeWakeLocks_{0};
};

} // namespace services
} // namespace ersa
