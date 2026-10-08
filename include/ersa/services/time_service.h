#pragma once

#include "ersa/common/types.h"
#include "ersa/hal/rtc.h"
#include "ersa/events/event_bus.h"
#include <stdint.h>

namespace ersa {
namespace services {

/**
 * Coordinates wall-clock state, source priority, and minute notifications.
 *
 * The RTC is the immediate timekeeper; this service validates all external
 * timestamps before writing it and prevents lower-trust sources from undoing a
 * stronger update. The default event subscription keeps BLE/network adapters
 * decoupled from RTC hardware while producing one normalized minute event for
 * watchfaces and apps.
 */
class TimeService {
public:
    /** Bind an RTC HAL and event bus; no hardware is touched until init(). */
    explicit TimeService(hal::IRtc& rtc, events::EventBus& bus = events::EventBus::instance());

    /** Initialize the RTC, seed minute tracking, and subscribe to time requests. */
    Result<void> init();
    /**
     * Poll the RTC at a bounded cadence and publish a MinuteTick on rollover.
     * @param currentUptimeMs Monotonic uptime used for interval tracking.
     */
    void tick(uint32_t currentUptimeMs);

    /**
     * Read current RTC wall time. The RTC stores the local wall clock chosen by
     * this firmware, so the timezone offset is metadata for network conversions
     * and display policy rather than an extra offset applied on every read.
     */
    hal::TimePoint now();
    /** Set time as a manual, highest-priority update after validating its range. */
    Result<void> setEpoch(uint32_t epochSeconds);
    /**
     * Validate and apply an external timestamp if its source has sufficient
     * priority. Manual > BLE/companion > network prevents delayed NTP responses
     * from replacing a newer phone-provided clock value.
     * @return True when the RTC accepted and stored the timestamp.
     */
    bool submitTime(events::TimeSource source, uint32_t epochSeconds);
    /** Adjust using calendar fields when no epoch value is supplied. */
    Result<void> adjust(const hal::TimePoint& time);

    /** Return the RTC HAL's current health report without retrying initialization. */
    bool isRtcHealthy() const;
    /**
     * Return whether a valid manual, phone, or network timestamp was accepted
     * since boot; a responding RTC alone does not count as synchronization.
     */
    bool hasSynchronizedTime() const;
    /** Return floor(epoch/60) for the most recently observed or written time. */
    uint32_t lastMinute() const;

    /** Configure local UTC offset used when converting network calendar values. */
    void setTimezoneOffset(int16_t offsetMinutes);
    /** Read the configured local UTC offset, where east of UTC is positive. */
    int16_t getTimezoneOffset() const;

    /** Return the process-wide service installed during board initialization. */
    static TimeService& instance();
    /**
     * Install a non-owning process-wide pointer for compatibility accessors.
     * The caller must keep the service alive until it replaces or clears it.
     */
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
