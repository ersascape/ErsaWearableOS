#include "ersa/services/time_service.h"
#if defined(ARDUINO)
#include "core/debug_log.h"
#endif

namespace ersa {
namespace services {

static TimeService* s_timeServiceInstance = nullptr;

TimeService& TimeService::instance() {
    return *s_timeServiceInstance;
}

void TimeService::setInstance(TimeService* instance) {
    s_timeServiceInstance = instance;
}

TimeService::TimeService(hal::IRtc& rtc, events::EventBus& bus)
    : rtc_(rtc), bus_(bus) {}

Result<void> TimeService::init() {
    Result<void> res = rtc_.init();
    hal::TimePoint current = rtc_.now();
    lastMinute_ = current.epoch / 60;
    if (!timeSyncSubscription_) {
        timeSyncSubscription_ = bus_.subscribe(events::EventType::TimeSync, onTimeSyncEvent, this);
    }
    return res;
}

void TimeService::onTimeSyncEvent(const events::Event& event, void* userData) {
    auto* self = static_cast<TimeService*>(userData);
    if (self && event.type == events::EventType::TimeSync)
        self->submitTime(event.time.source, event.time.epoch);
}

bool TimeService::submitTime(events::TimeSource source, uint32_t epochSeconds) {
    // The watch clock/DS3231 is intentionally constrained to valid dates
    // supported by the installed RTC and current firmware.
    if (epochSeconds < 1704067200UL || epochSeconds >= 4102444800UL) {
#if defined(ARDUINO)
        DebugLog::log("TIME: rejected source=%u epoch=%lu (outside supported range)",
                      unsigned(source), static_cast<unsigned long>(epochSeconds));
#endif
        return false;
    }
    const auto priority = [](events::TimeSource candidate) -> uint8_t {
        switch (candidate) {
            case events::TimeSource::Manual: return 3;
            case events::TimeSource::BleCurrentTime:
            case events::TimeSource::Companion: return 2;
            case events::TimeSource::Network: return 1;
            default: return 0;
        }
    };
    if (priority(source) == 0 || priority(source) < priority(selectedSource_)) {
#if defined(ARDUINO)
        DebugLog::log("TIME: rejected source=%u; selected source=%u has equal or higher priority",
                      unsigned(source), unsigned(selectedSource_));
#endif
        return false;
    }
    Result<void> result = rtc_.setEpoch(epochSeconds);
    if (!result.isOk()) {
#if defined(ARDUINO)
        DebugLog::log("TIME: RTC rejected source=%u epoch=%lu error=%d",
                      unsigned(source), static_cast<unsigned long>(epochSeconds),
                      int(result.error().code));
#endif
        return false;
    }
    const hal::TimePoint tp = rtc_.now();
    lastMinute_ = tp.epoch / 60;
    selectedSource_ = source;
#if defined(ARDUINO)
    DebugLog::log("TIME: accepted source=%u epoch=%lu", unsigned(source),
                  static_cast<unsigned long>(epochSeconds));
#endif
    return true;
}

void TimeService::tick(uint32_t currentUptimeMs) {
    // Poll RTC periodically (every 1000ms)
    if (currentUptimeMs - lastPollMs_ >= 1000) {
        lastPollMs_ = currentUptimeMs;
        hal::TimePoint tp = rtc_.now();
        uint32_t currentMin = tp.epoch / 60;

        if (currentMin != lastMinute_) {
            lastMinute_ = currentMin;

            events::TimePayload td;
            td.epoch = tp.epoch;
            td.year = tp.year;
            td.month = tp.month;
            td.day = tp.day;
            td.hour = tp.hour;
            td.minute = tp.minute;
            td.second = tp.second;

            bus_.publish(events::Event::createMinuteTick(td, currentUptimeMs));
        }
    }
}

hal::TimePoint TimeService::now() {
    return rtc_.now();
}

Result<void> TimeService::setEpoch(uint32_t epochSeconds) {
    if (submitTime(events::TimeSource::Manual, epochSeconds)) return Result<void>();
    return Result<void>(ErrorCode::InvalidParam, "invalid or rejected time source value");
}

Result<void> TimeService::adjust(const hal::TimePoint& time) {
    if (time.epoch) return setEpoch(time.epoch);
    Result<void> result = rtc_.adjust(time);
    if (!result.isOk()) return result;
    const hal::TimePoint adjusted = rtc_.now();
    lastMinute_ = adjusted.epoch / 60;
    selectedSource_ = events::TimeSource::Manual;
    return Result<void>();
}

bool TimeService::isRtcHealthy() const {
    return rtc_.isHealthy();
}

bool TimeService::hasSynchronizedTime() const {
    return selectedSource_ != events::TimeSource::Unknown;
}

uint32_t TimeService::lastMinute() const {
    return lastMinute_;
}

void TimeService::setTimezoneOffset(int16_t offsetMinutes) {
    tzOffsetMin_ = offsetMinutes;
}

int16_t TimeService::getTimezoneOffset() const {
    return tzOffsetMin_;
}

} // namespace services
} // namespace ersa
