#include "watch_clock.h"
#include "ersa/board/board.h"
#include "debug_log.h"
#include <Wire.h>

namespace {
RTC_DS3231 rtc;
bool online = false, adjusted = false, oscillatorStopped = false;
DateTime cachedTime(2026, 9, 28, 0, 0, 0);
uint32_t lastReadMs = 0;
uint32_t lastRtcPollMs = 0;

constexpr bool SET_FROM_BUILD = false;
constexpr uint32_t RTC_RECONCILE_INTERVAL_MS = 60000;

bool valid(const DateTime& time) {
    return time.isValid() && time.year() >= 2024 && time.year() <= 2099;
}

bool responds() {
    Wire.beginTransmission(0x68);
    const uint8_t error = Wire.endTransmission();
    if (error) DebugLog::log("RTC address=0x68 I2C error=%u", unsigned(error));
    return error == 0;
}
} // namespace

void WatchClock::begin() {
    Wire.begin(ersa::board::Board::current().getPins().i2c().sda.number, ersa::board::Board::current().getPins().i2c().scl.number);
    Wire.setClock(100000);
    Wire.setTimeOut(50);

    if (rtc.begin(&Wire) && responds()) {
        const DateTime rtcVal = rtc.now();
        oscillatorStopped = rtc.lostPower();
        if (oscillatorStopped)
            DebugLog::log("RTC oscillator-stop flag set; retaining software time until a trusted sync");
        if (!SET_FROM_BUILD && valid(rtcVal)) {
            cachedTime = rtcVal;
            online = true;
            DebugLog::log("RTC online: %04u-%02u-%02u %02u:%02u:%02u",
                          unsigned(rtcVal.year()), unsigned(rtcVal.month()), unsigned(rtcVal.day()),
                          unsigned(rtcVal.hour()), unsigned(rtcVal.minute()), unsigned(rtcVal.second()));
        } else {
            // Only set to build time if uninitialized/corrupt (year < 2024) or explicitly forced
            const DateTime buildDt(F(__DATE__), F(__TIME__));
            rtc.adjust(buildDt);
            cachedTime = buildDt;
            online = true;
            adjusted = true;
            DebugLog::log("RTC uninitialized; adjusted to build time %04u-%02u-%02u %02u:%02u:%02u",
                          unsigned(buildDt.year()), unsigned(buildDt.month()), unsigned(buildDt.day()),
                          unsigned(buildDt.hour()), unsigned(buildDt.minute()), unsigned(buildDt.second()));
        }
    } else {
        cachedTime = DateTime(F(__DATE__), F(__TIME__));
        online = false;
        DebugLog::log("RTC not detected on I2C; using build time fallback");
    }
    lastReadMs = millis();
    lastRtcPollMs = millis();
}

void WatchClock::tick() {
    const uint32_t nowMs = millis();
    // Time reads are served from cachedTime + millis(). Reconcile against the
    // DS3231 once a minute instead of doing two I2C transactions every second.
    if (nowMs - lastRtcPollMs >= RTC_RECONCILE_INTERVAL_MS) {
        lastRtcPollMs = nowMs;
        if (!oscillatorStopped && responds()) {
            if (rtc.lostPower()) {
                oscillatorStopped = true;
                DebugLog::log("RTC oscillator-stop flag set while running; retaining software time until a trusted sync");
                return;
            }
            const DateTime rtcVal = rtc.now();
            if (valid(rtcVal)) {
                const DateTime softwareVal(cachedTime.unixtime() +
                    (nowMs - lastReadMs) / 1000);
                const int32_t driftSeconds = static_cast<int32_t>(
                    int64_t(rtcVal.unixtime()) - int64_t(softwareVal.unixtime()));
                DebugLog::log("RTC poll: chip=%04u-%02u-%02u %02u:%02u:%02u software=%04u-%02u-%02u %02u:%02u:%02u drift=%ld s",
                              unsigned(rtcVal.year()), unsigned(rtcVal.month()), unsigned(rtcVal.day()),
                              unsigned(rtcVal.hour()), unsigned(rtcVal.minute()), unsigned(rtcVal.second()),
                              unsigned(softwareVal.year()), unsigned(softwareVal.month()), unsigned(softwareVal.day()),
                              unsigned(softwareVal.hour()), unsigned(softwareVal.minute()), unsigned(softwareVal.second()),
                              static_cast<long>(driftSeconds));
                cachedTime = rtcVal;
                lastReadMs = nowMs;
                online = true;
            }
        }
    }
}

void WatchClock::resync() {
    // A valid-looking timestamp can remain in the DS3231 after its oscillator
    // stops. Do not replace the advancing software clock with that stale value.
    if (oscillatorStopped) return;
    if (!responds()) return;
    if (rtc.lostPower()) {
        oscillatorStopped = true;
        DebugLog::log("RTC oscillator-stop flag set after sleep; retaining software time until a trusted sync");
        return;
    }
    const DateTime rtcVal = rtc.now();
    if (!valid(rtcVal)) return;
    const uint32_t nowMs = millis();
    cachedTime = rtcVal;
    lastReadMs = nowMs;
    lastRtcPollMs = nowMs;
    online = true;
}

DateTime WatchClock::now() {
    const uint32_t elapsedSec = (millis() - lastReadMs) / 1000;
    return DateTime(cachedTime.unixtime() + elapsedSec);
}

bool WatchClock::healthy() { return online && !oscillatorStopped; }
bool WatchClock::setAtBoot() { return adjusted; }

void WatchClock::adjust(const DateTime& time) {
    if (!valid(time)) return;
    cachedTime = time;
    lastReadMs = millis();
    lastRtcPollMs = millis();
    if (responds()) {
        rtc.adjust(time);
        oscillatorStopped = false;
        online = true;
        DebugLog::log("RTC adjusted to %04u-%02u-%02u %02u:%02u:%02u",
                      unsigned(time.year()), unsigned(time.month()), unsigned(time.day()),
                      unsigned(time.hour()), unsigned(time.minute()), unsigned(time.second()));
    } else {
        DebugLog::log("RTC adjust I2C unreachable; software time set");
    }
}

void WatchClock::setEpoch(uint32_t epochSeconds) {
    adjust(DateTime(epochSeconds));
}
