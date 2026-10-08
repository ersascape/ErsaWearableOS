#pragma once
#include "ersa/common/calendar_time.h"

namespace WatchClock {

// Refresh the selected RTC HAL after a wake where platform uptime may pause.
void resync();
CalendarTime now();
bool healthy();

// Real-time synchronization (NTP or BLE phone time sync)
void setEpoch(uint32_t epochSeconds);
void adjust(const CalendarTime& time);

} // namespace WatchClock
