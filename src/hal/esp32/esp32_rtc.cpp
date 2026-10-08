#if defined(ARDUINO)

#include "hal/esp32/esp32_rtc.h"
#include "core/debug_log.h"
#include <Arduino.h>
#include <Wire.h>

namespace ersa {
namespace hal {

Esp32Rtc::Esp32Rtc(int sda, int scl)
    : sda_(sda), scl_(scl) {}

TimePoint Esp32Rtc::toTimePoint(const DateTime& dt) {
    return TimePoint(
        dt.year(), dt.month(), dt.day(),
        dt.hour(), dt.minute(), dt.second(),
        dt.dayOfTheWeek(), dt.unixtime()
    );
}

DateTime Esp32Rtc::toDateTime(const TimePoint& tp) {
    if (tp.epoch > 0) {
        return DateTime(tp.epoch);
    }
    return DateTime(tp.year, tp.month, tp.day, tp.hour, tp.minute, tp.second);
}

Result<void> Esp32Rtc::init() {
    Wire.begin(sda_, scl_);
    Wire.setClock(100000);
    Wire.setTimeOut(50);
    if (!rtc_.begin(&Wire) || !responds()) {
        cachedTime_ = DateTime(F(__DATE__), F(__TIME__));
        online_ = false;
        lastReadMs_ = lastRtcPollMs_ = millis();
        DebugLog::log("RTC not detected on I2C; using build time fallback");
        return Result<void>();
    }

    const DateTime rtcValue = rtc_.now();
    oscillatorStopped_ = rtc_.lostPower();
    if (oscillatorStopped_)
        DebugLog::log("RTC oscillator-stop flag set; retaining software time until a trusted sync");
    if (!valid(rtcValue)) {
        cachedTime_ = DateTime(F(__DATE__), F(__TIME__));
        rtc_.adjust(cachedTime_);
        DebugLog::log("RTC uninitialized; adjusted to build time");
    } else {
        cachedTime_ = rtcValue;
        DebugLog::log("RTC online: %04u-%02u-%02u %02u:%02u:%02u",
                      unsigned(rtcValue.year()), unsigned(rtcValue.month()), unsigned(rtcValue.day()),
                      unsigned(rtcValue.hour()), unsigned(rtcValue.minute()), unsigned(rtcValue.second()));
    }
    online_ = true;
    lastReadMs_ = lastRtcPollMs_ = millis();
    return Result<void>();
}

TimePoint Esp32Rtc::now() {
    const uint32_t nowMs = millis();
    reconcile(nowMs, false);
    const uint32_t elapsedSeconds = (nowMs - lastReadMs_) / 1000;
    return toTimePoint(DateTime(cachedTime_.unixtime() + elapsedSeconds));
}

Result<void> Esp32Rtc::adjust(const TimePoint& time) {
    const DateTime value = toDateTime(time);
    if (!valid(value)) return Result<void>(ErrorCode::InvalidParam, "RTC timestamp is outside the supported range");
    cachedTime_ = value;
    lastReadMs_ = lastRtcPollMs_ = millis();
    driftMeasured_ = false;
    lastDriftSeconds_ = 0;
    if (online_ && responds()) {
        rtc_.adjust(value);
        oscillatorStopped_ = false;
        DebugLog::log("RTC adjusted to %04u-%02u-%02u %02u:%02u:%02u",
                      unsigned(value.year()), unsigned(value.month()), unsigned(value.day()),
                      unsigned(value.hour()), unsigned(value.minute()), unsigned(value.second()));
    } else {
        DebugLog::log("RTC adjust I2C unreachable; software time set");
    }
    return Result<void>();
}

Result<void> Esp32Rtc::setEpoch(uint32_t epochSeconds) {
    return adjust(toTimePoint(DateTime(epochSeconds)));
}

bool Esp32Rtc::isHealthy() const {
    return online_ && !oscillatorStopped_;
}

RtcDiagnostics Esp32Rtc::diagnostics() {
    RtcDiagnostics result{};
    result.time = now();
    result.oscillatorStopped = oscillatorStopped_;
    result.hardwareReadable = online_ && responds();
    result.driftMeasured = driftMeasured_;
    result.driftSeconds = lastDriftSeconds_;
    if (result.hardwareReadable) {
        const DateTime chipTime = rtc_.now();
        if (valid(chipTime)) result.chipTime = toTimePoint(chipTime);
        else
            result.hardwareReadable = false;
    }
    return result;
}

bool Esp32Rtc::valid(const DateTime& time) {
    return time.isValid() && time.year() >= 2024 && time.year() <= 2099;
}

bool Esp32Rtc::responds() const {
    Wire.beginTransmission(0x68);
    const uint8_t error = Wire.endTransmission();
    if (error) DebugLog::log("RTC address=0x68 I2C error=%u", unsigned(error));
    return error == 0;
}

void Esp32Rtc::resync() {
    reconcile(millis(), true);
}

void Esp32Rtc::reconcile(uint32_t nowMs, bool force) {
    if (oscillatorStopped_ || (!force && uint32_t(nowMs - lastRtcPollMs_) < 60000)) return;
    lastRtcPollMs_ = nowMs;
    if (!online_) {
        if (!rtc_.begin(&Wire) || !responds()) return;
        online_ = true;
    } else if (!responds()) {
        online_ = false;
        return;
    }
    if (rtc_.lostPower()) {
        oscillatorStopped_ = true;
        DebugLog::log("RTC oscillator-stop flag set; retaining software time until a trusted sync");
        return;
    }
    const DateTime rtcValue = rtc_.now();
    if (!valid(rtcValue)) return;
    const DateTime softwareValue(cachedTime_.unixtime() + uint32_t(nowMs - lastReadMs_) / 1000);
    const int32_t driftSeconds = static_cast<int32_t>(int64_t(rtcValue.unixtime()) - int64_t(softwareValue.unixtime()));
    lastDriftSeconds_ = driftSeconds;
    driftMeasured_ = true;
    DebugLog::log("RTC poll: chip=%04u-%02u-%02u %02u:%02u:%02u software=%04u-%02u-%02u %02u:%02u:%02u drift=%ld s",
                  unsigned(rtcValue.year()), unsigned(rtcValue.month()), unsigned(rtcValue.day()),
                  unsigned(rtcValue.hour()), unsigned(rtcValue.minute()), unsigned(rtcValue.second()),
                  unsigned(softwareValue.year()), unsigned(softwareValue.month()), unsigned(softwareValue.day()),
                  unsigned(softwareValue.hour()), unsigned(softwareValue.minute()), unsigned(softwareValue.second()),
                  static_cast<long>(driftSeconds));
    cachedTime_ = rtcValue;
    lastReadMs_ = nowMs;
    online_ = true;
}

} // namespace hal
} // namespace ersa

#endif // ARDUINO
