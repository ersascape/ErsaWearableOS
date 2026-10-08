#pragma once

#if defined(ARDUINO)

#include "ersa/hal/rtc.h"
#include <RTClib.h>

namespace ersa {
namespace hal {

/**
 * ESP32 I2C adapter for the Terra DS3231 real-time clock.
 * RTClib types are confined to this platform implementation; services exchange
 * `TimePoint` values and can therefore use a host fake or another RTC driver.
 */
class Esp32Rtc : public IRtc {
public:
    /** Bind SDA/SCL pins from the board pin map before calling init(). */
    Esp32Rtc(int sda = 6, int scl = 7);
    ~Esp32Rtc() override = default;

    Result<void> init() override;
    TimePoint now() override;
    Result<void> adjust(const TimePoint& time) override;
    Result<void> setEpoch(uint32_t epochSeconds) override;
    bool isHealthy() const override;
    RtcDiagnostics diagnostics() override;
    /** Re-read the DS3231 after automatic sleep may have paused MCU uptime. */
    void resync() override;

private:
    int sda_, scl_;
    RTC_DS3231 rtc_;
    DateTime cachedTime_{2026, 9, 28, 0, 0, 0};
    uint32_t lastReadMs_{0};
    uint32_t lastRtcPollMs_{0};
    bool online_{false};
    bool oscillatorStopped_{false};
    bool driftMeasured_{false};
    int32_t lastDriftSeconds_{0};

    static TimePoint toTimePoint(const DateTime& dt);
    static DateTime toDateTime(const TimePoint& tp);
    static bool valid(const DateTime& time);
    bool responds() const;
    void reconcile(uint32_t nowMs, bool force);
};

} // namespace hal
} // namespace ersa

#endif // ARDUINO
