#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace ersa::common {

/** Small Gregorian calendar value used by apps without a third-party date library. */
class CalendarTime {
public:
    /** Construct an invalid zero value. */
    constexpr CalendarTime() = default;
    /** Construct from calendar fields. */
    constexpr CalendarTime(uint16_t y, uint8_t m, uint8_t d, uint8_t h, uint8_t min, uint8_t s)
        : year_(y), month_(m), day_(d), hour_(h), minute_(min), second_(s) {}
    /** Construct from Unix seconds. */
    explicit CalendarTime(uint32_t epoch) { fromEpoch(epoch); }
    /** Construct from the compiler's `__DATE__` and `__TIME__` strings. */
    CalendarTime(const char* date, const char* time) { fromBuildStrings(date, time); }

    constexpr uint16_t year() const { return year_; }
    constexpr uint8_t month() const { return month_; }
    constexpr uint8_t day() const { return day_; }
    constexpr uint8_t hour() const { return hour_; }
    constexpr uint8_t minute() const { return minute_; }
    constexpr uint8_t second() const { return second_; }
    constexpr bool isValid() const {
        return year_ >= 2000 && year_ <= 2199 && month_ >= 1 && month_ <= 12 &&
               day_ >= 1 && day_ <= daysInMonth(year_, month_) && hour_ <= 23 &&
               minute_ <= 59 && second_ <= 59;
    }
    /** Return Sunday=0 through Saturday=6. */
    /** Return the weekday using Sunday=0 through Saturday=6. */
    uint8_t dayOfTheWeek() const;
    /** Convert valid calendar fields to Unix seconds; invalid values return zero. */
    /** Convert a valid calendar value to Unix seconds; invalid values return zero. */
    uint32_t unixtime() const;

private:
    static constexpr bool leap(uint16_t y) {
        return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
    }
    static constexpr uint8_t daysInMonth(uint16_t y, uint8_t m) {
        return m == 2 ? (leap(y) ? 29 : 28) :
               (m == 4 || m == 6 || m == 9 || m == 11 ? 30 : 31);
    }
    int64_t daysSinceEpoch() const;
    void fromEpoch(uint32_t epoch) {
        const time_t raw = static_cast<time_t>(epoch);
        struct tm value{};
        if (!gmtime_r(&raw, &value)) return;
        year_ = static_cast<uint16_t>(value.tm_year + 1900);
        month_ = static_cast<uint8_t>(value.tm_mon + 1);
        day_ = static_cast<uint8_t>(value.tm_mday);
        hour_ = static_cast<uint8_t>(value.tm_hour);
        minute_ = static_cast<uint8_t>(value.tm_min);
        second_ = static_cast<uint8_t>(value.tm_sec);
    }
    void fromBuildStrings(const char* date, const char* time) {
        if (!date || !time) return;
        static constexpr const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        char monthName[4]{};
        unsigned d = 0, y = 0, h = 0, m = 0, s = 0;
        if (sscanf(date, "%3s %u %u", monthName, &d, &y) != 3 ||
            sscanf(time, "%u:%u:%u", &h, &m, &s) != 3) return;
        for (uint8_t i = 0; i < 12; ++i) {
            if (strcmp(monthName, months[i]) == 0) {
                year_ = static_cast<uint16_t>(y); month_ = static_cast<uint8_t>(i + 1);
                day_ = static_cast<uint8_t>(d); hour_ = static_cast<uint8_t>(h);
                minute_ = static_cast<uint8_t>(m); second_ = static_cast<uint8_t>(s);
                return;
            }
        }
    }

    uint16_t year_{0};
    uint8_t month_{0}, day_{0}, hour_{0}, minute_{0}, second_{0};
};

} // namespace ersa::common

using CalendarTime = ersa::common::CalendarTime;
