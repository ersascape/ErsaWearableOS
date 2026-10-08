#pragma once

#include "ersa/common/types.h"
#include "ersa/hal/rtc.h"
#include "ersa/events/event_bus.h"
#include <stdint.h>

namespace ersa {
namespace services {

/// Coordinates RTC time, external time sources, and minute-boundary events.
class TimeService {
public:
    /// Manage the supplied RTC and event bus.
    explicit TimeService(hal::IRtc& rtc, events::EventBus& bus = events::EventBus::instance());

    /// Initialize the RTC and publish initial health state.
    Result<void> init();
    /// Advance time-source and minute-event processing.
    void tick(uint32_t currentUptimeMs);

    /// Return the current local time after applying the configured offset.
    hal::TimePoint now();
    /// Set the RTC using Unix epoch seconds.
    Result<void> setEpoch(uint32_t epochSeconds);
    /// Submit a timestamp from the specified external source.
    bool submitTime(events::TimeSource source, uint32_t epochSeconds);
    /// Adjust the RTC to an explicit calendar time.
    Result<void> adjust(const hal::TimePoint& time);

    /// Return the latest RTC health state.
    bool isRtcHealthy() const;
    /// Return the most recently published minute index.
    uint32_t lastMinute() const;

    /// Set the local timezone offset in minutes from UTC.
    void setTimezoneOffset(int16_t offsetMinutes);
    /// Return the configured local timezone offset in minutes.
    int16_t getTimezoneOffset() const;

    /// Return the installed process-wide time service.
    static TimeService& instance();
    /// Install the process-wide time service for legacy service accessors.
    static void setInstance(TimeService* instance);

private:
    hal::IRtc& rtc_;
    events::EventBus& bus_;
    int16_t tzOffsetMin_{0};
    uint32_t lastMinute_{UINT32_MAX};
    uint32_t lastPollMs_{0};
    events::TimeSource selectedSource_{events::TimeSource::Unknown};
    events::SubscriptionId timeSyncSubscription_{0};
    static void onTimeSyncEvent(const events::Event& event, void* userData);
};

} // namespace services
} // namespace ersa
