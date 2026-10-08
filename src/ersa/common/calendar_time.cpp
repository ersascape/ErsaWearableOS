#include "ersa/common/calendar_time.h"

namespace ersa::common {

int64_t CalendarTime::daysSinceEpoch() const {
    int year = year_;
    const unsigned month = month_;
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned shiftedMonth = month > 2 ? month - 3 : month + 9;
    const unsigned dayOfYear = (153 * shiftedMonth + 2) / 5 + day_ - 1;
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097LL + static_cast<int>(dayOfEra) - 719468;
}

uint8_t CalendarTime::dayOfTheWeek() const {
    const int64_t weekday = (daysSinceEpoch() + 4) % 7;
    return static_cast<uint8_t>(weekday < 0 ? weekday + 7 : weekday);
}

uint32_t CalendarTime::unixtime() const {
    if (!isValid()) return 0;
    return static_cast<uint32_t>(daysSinceEpoch() * 86400LL + hour_ * 3600 + minute_ * 60 + second_);
}

} // namespace ersa::common
