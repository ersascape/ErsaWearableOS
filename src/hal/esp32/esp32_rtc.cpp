#if defined(ARDUINO)

#include "hal/esp32/esp32_rtc.h"
#include "core/debug_log.h"
#include <Arduino.h>

namespace ersa {
namespace hal {

Esp32Rtc::Esp32Rtc(int sda, int scl)
    : sda_(sda), scl_(scl) {}

TimePoint Esp32Rtc::toTimePoint(const CalendarTime& dt) {
    return TimePoint(
        dt.year(), dt.month(), dt.day(),
        dt.hour(), dt.minute(), dt.second(),
        dt.dayOfTheWeek(), dt.unixtime()
    );
}

CalendarTime Esp32Rtc::toCalendarTime(const TimePoint& tp) {
    if (tp.epoch > 0) {
        return CalendarTime(tp.epoch);
    }
    return CalendarTime(tp.year, tp.month, tp.day, tp.hour, tp.minute, tp.second);
}

Result<void> Esp32Rtc::init() {
    if (!rtc_.begin(sda_, scl_) || !responds()) {
        cachedTime_ = CalendarTime(__DATE__, __TIME__);
        online_ = false;
        lastReadMs_ = lastRtcPollMs_ = millis();
        DebugLog::log("RTC not detected on I2C; using build time fallback");
        return Result<void>();
    }

    CalendarTime rtcValue{};
    if (!readChipTime(rtcValue)) {
        online_ = false;
        cachedTime_ = CalendarTime(__DATE__, __TIME__);
        lastReadMs_ = lastRtcPollMs_ = millis();
        DebugLog::log("RTC read failed on I2C; using build time fallback");
        return Result<void>();
    }
    bool lostPower = false;
    oscillatorStopped_ = !rtc_.lostPower(lostPower) || lostPower;
    if (oscillatorStopped_)
        DebugLog::log("RTC oscillator-stop flag set; retaining software time until a trusted sync");
    if (!valid(rtcValue)) {
        cachedTime_ = CalendarTime(__DATE__, __TIME__);
        writeChipTime(cachedTime_);
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
    return toTimePoint(CalendarTime(cachedTime_.unixtime() + elapsedSeconds));
}

Result<void> Esp32Rtc::adjust(const TimePoint& time) {
    const CalendarTime value = toCalendarTime(time);
    if (!valid(value)) return Result<void>(ErrorCode::InvalidParam, "RTC timestamp is outside the supported range");
    cachedTime_ = value;
    lastReadMs_ = lastRtcPollMs_ = millis();
    driftMeasured_ = false;
    lastDriftSeconds_ = 0;
    if (online_ && responds()) {
        writeChipTime(value);
        CalendarTime written{};
        readChipTime(written);
        const int64_t writeDelta = valid(written)
            ? int64_t(written.unixtime()) - int64_t(value.unixtime())
            : INT64_MAX;
        if (writeDelta >= -2 && writeDelta <= 2) {
            oscillatorStopped_ = false;
            DebugLog::log("RTC adjusted and verified: %04u-%02u-%02u %02u:%02u:%02u",
                          unsigned(written.year()), unsigned(written.month()), unsigned(written.day()),
                          unsigned(written.hour()), unsigned(written.minute()), unsigned(written.second()));
        } else {
            online_ = false;
            DebugLog::log("RTC write verification failed; chip readback delta=%lld s, software time retained",
                          static_cast<long long>(writeDelta));
        }
    } else {
        online_ = false;
        DebugLog::log("RTC adjust I2C unreachable; software time set");
    }
    return Result<void>();
}

Result<void> Esp32Rtc::setEpoch(uint32_t epochSeconds) {
    return adjust(toTimePoint(CalendarTime(epochSeconds)));
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
        CalendarTime chipTime{};
        readChipTime(chipTime);
        if (valid(chipTime)) result.chipTime = toTimePoint(chipTime);
        else
            result.hardwareReadable = false;
    }
    return result;
}

bool Esp32Rtc::valid(const CalendarTime& time) {
    return time.isValid() && time.year() >= 2024 && time.year() <= 2099;
}

bool Esp32Rtc::responds() {
    const bool detected = rtc_.probe();
    if (!detected) {
        DebugLog::log("RTC address=0x68 I2C probe failed (SDA GPIO%d=%d SCL GPIO%d=%d)",
                      sda_, digitalRead(sda_), scl_, digitalRead(scl_));
    }
    return detected;
}

bool Esp32Rtc::readChipTime(CalendarTime& time) {
    uint16_t year = 0;
    uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0, dayOfWeek = 0;
    if (!rtc_.read(year, month, day, hour, minute, second, dayOfWeek)) return false;
    time = CalendarTime(year, month, day, hour, minute, second);
    return valid(time);
}

bool Esp32Rtc::writeChipTime(const CalendarTime& time) {
    // DS3231 weekday numbering is 1=Sunday; CalendarTime uses Sunday=0.
    return rtc_.write(time.year(), time.month(), time.day(), time.hour(), time.minute(),
                      time.second(), static_cast<uint8_t>(time.dayOfTheWeek() + 1));
}

void Esp32Rtc::resync() {
    reconcile(millis(), true);
}

void Esp32Rtc::reconcile(uint32_t nowMs, bool force) {
    if (oscillatorStopped_ || (!force && uint32_t(nowMs - lastRtcPollMs_) < 60000)) return;
    lastRtcPollMs_ = nowMs;
    if (!online_) {
        if (!rtc_.begin(sda_, scl_) || !responds()) return;
        online_ = true;
    } else if (!responds()) {
        online_ = false;
        return;
    }
    bool lostPower = false;
    if (!rtc_.lostPower(lostPower) || lostPower) {
        oscillatorStopped_ = true;
        DebugLog::log("RTC oscillator-stop flag set; retaining software time until a trusted sync");
        return;
    }
    CalendarTime rtcValue{};
    if (!readChipTime(rtcValue)) return;
    const CalendarTime softwareValue(cachedTime_.unixtime() + uint32_t(nowMs - lastReadMs_) / 1000);
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
