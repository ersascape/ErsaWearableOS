#include "watch_clock.h"
#include "ersa/board/board.h"

namespace {

DateTime toDateTime(const ersa::hal::TimePoint& time) {
    if (time.epoch) return DateTime(time.epoch);
    return DateTime(time.year, time.month, time.day, time.hour, time.minute, time.second);
}

ersa::hal::TimePoint toTimePoint(const DateTime& time) {
    return ersa::hal::TimePoint(time.year(), time.month(), time.day(),
                                time.hour(), time.minute(), time.second(),
                                time.dayOfTheWeek(), time.unixtime());
}

ersa::hal::IRtc& rtc() {
    return ersa::board::Board::current().getRtc();
}

} // namespace

DateTime WatchClock::now() {
    return toDateTime(rtc().now());
}

bool WatchClock::healthy() {
    return rtc().isHealthy();
}

void WatchClock::resync() {
    rtc().resync();
}

void WatchClock::adjust(const DateTime& time) {
    rtc().adjust(toTimePoint(time));
}

void WatchClock::setEpoch(uint32_t epochSeconds) {
    rtc().setEpoch(epochSeconds);
}
