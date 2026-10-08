#pragma once
#include <stdint.h>
#include <stddef.h>

namespace WatchConfig {

/**
 * Fixed-size persisted device settings snapshot.
 * Arrays are bounded to avoid heap allocation and make NVS loading predictable;
 * setter functions sanitize and null-terminate strings before storing them.
 */
struct Config {
    char wifiSsid[33];
    char wifiPass[65];
    char caldavServer[129];
    char caldavUser[49];
    char caldavPass[65];
    char caldavCalendar[49];
    char caldavTodoPath[49];
    int16_t timezoneOffsetMin;
    bool militaryTime;
    uint8_t fullRefreshInterval;
    char apSsid[33];
    char apPass[33];
    uint16_t apTimeoutSec;
};

/** Load persisted values, validate ranges, and create defaults on first boot. */
void begin();
/** Return the process-wide immutable current settings snapshot. */
const Config& get();
/** Set station credentials, truncating safely to fixed config capacities. */
void setWifi(const char* ssid, const char* pass);
/** Set CalDAV endpoint/account and collection paths. */
void setCalDav(const char* server, const char* user, const char* pass, const char* calendar, const char* todoPath);
/** Set local UTC offset in minutes, within the supported civil-time range. */
void setTimezone(int16_t offsetMinutes);
/** Select 24-hour or 12-hour display formatting. */
void setTimeFormat(bool military24h);
/** Set configuration access point credentials and idle timeout. */
void setApConfig(const char* ssid, const char* pass, uint16_t timeoutSec);
/** Persist the active settings snapshot to nonvolatile storage. */
bool save();
/** Restore compiled defaults and persist them to storage. */
void resetDefaults();

} // namespace WatchConfig
