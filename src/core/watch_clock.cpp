#include "watch_clock.h"
#include "ersa/board/board.h"

namespace {

CalendarTime toCalendarTime(const ersa::hal::TimePoint& time) {
    if (time.epoch) return CalendarTime(time.epoch);
    return CalendarTime(time.year, time.month, time.day, time.hour, time.minute, time.second);
}

ersa::hal::TimePoint toTimePoint(const CalendarTime& time) {
    return ersa::hal::TimePoint(time.year(), time.month(), time.day(),
                                time.hour(), time.minute(), time.second(),
                                time.dayOfTheWeek(), time.unixtime());
}

ersa::hal::IRtc& rtc() {
    return ersa::board::Board::current().getRtc();
}

} // namespace

CalendarTime WatchClock::now() {
    return toCalendarTime(rtc().now());
}

bool WatchClock::healthy() {
    return rtc().isHealthy();
}

void WatchClock::resync() {
    rtc().resync();
}

void WatchClock::adjust(const CalendarTime& time) {
    rtc().adjust(toTimePoint(time));
}

void WatchClock::setEpoch(uint32_t epochSeconds) {
    rtc().setEpoch(epochSeconds);
}
