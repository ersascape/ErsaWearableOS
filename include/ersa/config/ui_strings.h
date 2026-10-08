#pragma once

namespace ersa {
namespace strings {

// Status & Progress Messages
inline constexpr const char* MSG_READY             = "Ready";
inline constexpr const char* MSG_WIFI_CONNECTING   = "Connecting WiFi...";
inline constexpr const char* MSG_WIFI_FAILED       = "WiFi Connect Failed";
inline constexpr const char* MSG_WIFI_NO_SSID      = "WiFi SSID not set";
inline constexpr const char* MSG_SYNCING_NTP       = "Syncing NTP...";
inline constexpr const char* MSG_NTP_SYNCED        = "NTP Time Synced";
inline constexpr const char* MSG_HTTP_TIME_SYNCED  = "HTTP Time Synced";
inline constexpr const char* MSG_TIME_SYNC_FAILED  = "Time Sync Failed";
inline constexpr const char* MSG_QUERYING_CALDAV   = "Querying CalDAV...";
inline constexpr const char* MSG_CALDAV_FAILED     = "CalDAV HTTP Failed";
inline constexpr const char* MSG_SYNC_COMPLETE     = "Sync Complete";
inline constexpr const char* MSG_SYNCING           = "syncing...";
inline constexpr const char* MSG_LOW_BATT          = "low batt";
inline constexpr const char* MSG_USB_POWER         = "USB power";
inline constexpr const char* MSG_ONLINE            = "online";
inline constexpr const char* MSG_OFFLINE           = "offline";

// Empty State Messages
inline constexpr const char* MSG_NO_EVENTS_TODAY   = "no events today";
inline constexpr const char* MSG_NO_TASKS          = "no tasks found";
inline constexpr const char* MSG_PRESS_SYNC_CALDAV = "press B2 to sync CalDAV";

// App Titles & Drawer Labels
inline constexpr const char* APP_TITLE_CLOCK       = "clock";
inline constexpr const char* APP_TITLE_NOW_PLAYING = "now playing";
inline constexpr const char* APP_TITLE_CALLS       = "calls";
inline constexpr const char* APP_TITLE_NOTIFS      = "notifications";
inline constexpr const char* APP_TITLE_CALENDAR    = "calendar";
inline constexpr const char* APP_TITLE_AGENDA      = "agenda";
inline constexpr const char* APP_TITLE_TASKS       = "tasks";
inline constexpr const char* APP_TITLE_HOTSPOT     = "hotspot";
inline constexpr const char* APP_TITLE_STATUS      = "status";
inline constexpr const char* APP_DRAWER_HEADER     = "apps";

// Navigation & Button Hints
inline constexpr const char* NAV_DRAWER_FOOTER     = "scroll B1   select B2";
inline constexpr const char* NAV_CALENDAR_FOOTER   = "B1 +month / hold drawer   B2 today";
inline constexpr const char* NAV_AGENDA_FOOTER     = "B1 scroll / hold drawer   B2 sync";
inline constexpr const char* NAV_AGENDA_EMPTY_FOOT = "B2 sync   hold B1 drawer";
inline constexpr const char* NAV_TODO_FOOTER       = "B1 scroll / hold drawer   B2 toggle";
inline constexpr const char* NAV_TODO_EMPTY_FOOT   = "B2 sync   hold B1 drawer";
inline constexpr const char* NAV_STATUS_FOOTER     = "sync B2   menu B1";

} // namespace strings
} // namespace ersa
