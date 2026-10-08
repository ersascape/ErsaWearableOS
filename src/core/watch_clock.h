#pragma once
#include <RTClib.h>

namespace WatchClock {

// Refresh the selected RTC HAL after a wake where platform uptime may pause.
void resync();
DateTime now();
bool healthy();

// Real-time synchronization (NTP or BLE phone time sync)
void setEpoch(uint32_t epochSeconds);
void adjust(const DateTime& time);

} // namespace WatchClock
