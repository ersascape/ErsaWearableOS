#include "watch_config.h"
#include "debug_log.h"
#include "ersa/config/system_defaults.h"
#include "ersa/services/storage_service.h"
#include <string.h>

namespace WatchConfig {

namespace {
Config activeConfig;
ersa::services::StorageService& storage() {
    return ersa::services::StorageService::instance();
}

void safeCopy(char* dest, const char* src, size_t maxLen) {
    if (!dest || maxLen == 0) return;
    if (!src) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, src, maxLen - 1);
    dest[maxLen - 1] = '\0';
}

uint8_t sanitizeLoadedConfig() {
    uint8_t repaired = 0;
    if (activeConfig.timezoneOffsetMin < -840 || activeConfig.timezoneOffsetMin > 840) {
        activeConfig.timezoneOffsetMin = ersa::config::DEFAULT_TIMEZONE_OFFSET_MIN;
        ++repaired;
    }
    if (activeConfig.fullRefreshInterval == 0 || activeConfig.fullRefreshInterval > 60) {
        activeConfig.fullRefreshInterval = ersa::config::DEFAULT_FULL_REFRESH_CYCLES;
        ++repaired;
    }
    if (activeConfig.apSsid[0] == '\0') {
        safeCopy(activeConfig.apSsid, ersa::config::DEFAULT_AP_SSID, sizeof(activeConfig.apSsid));
        ++repaired;
    }
    if (strlen(activeConfig.apPass) < 8) {
        safeCopy(activeConfig.apPass, ersa::config::DEFAULT_AP_PASS, sizeof(activeConfig.apPass));
        ++repaired;
    }
    if (activeConfig.apTimeoutSec < 30 || activeConfig.apTimeoutSec > 3600) {
        activeConfig.apTimeoutSec = ersa::config::DEFAULT_AP_TIMEOUT_SEC;
        ++repaired;
    }
    return repaired;
}
} // namespace

void resetDefaults() {
    safeCopy(activeConfig.wifiSsid, ersa::config::DEFAULT_WIFI_SSID, sizeof(activeConfig.wifiSsid));
    safeCopy(activeConfig.wifiPass, ersa::config::DEFAULT_WIFI_PASS, sizeof(activeConfig.wifiPass));
    safeCopy(activeConfig.caldavServer, ersa::config::DEFAULT_CALDAV_SERVER, sizeof(activeConfig.caldavServer));
    safeCopy(activeConfig.caldavUser, ersa::config::DEFAULT_CALDAV_USER, sizeof(activeConfig.caldavUser));
    safeCopy(activeConfig.caldavPass, ersa::config::DEFAULT_CALDAV_PASS, sizeof(activeConfig.caldavPass));
    safeCopy(activeConfig.caldavCalendar, ersa::config::DEFAULT_CALDAV_CALENDAR, sizeof(activeConfig.caldavCalendar));
    safeCopy(activeConfig.caldavTodoPath, ersa::config::DEFAULT_CALDAV_TODO, sizeof(activeConfig.caldavTodoPath));
    activeConfig.timezoneOffsetMin = ersa::config::DEFAULT_TIMEZONE_OFFSET_MIN;
    activeConfig.militaryTime = ersa::config::DEFAULT_MILITARY_TIME;
    activeConfig.fullRefreshInterval = ersa::config::DEFAULT_FULL_REFRESH_CYCLES;
    safeCopy(activeConfig.apSsid, ersa::config::DEFAULT_AP_SSID, sizeof(activeConfig.apSsid));
    safeCopy(activeConfig.apPass, ersa::config::DEFAULT_AP_PASS, sizeof(activeConfig.apPass));
    activeConfig.apTimeoutSec = ersa::config::DEFAULT_AP_TIMEOUT_SEC;
}

void begin() {
    resetDefaults();
    const auto storageInit = storage().init();
    if (storageInit.isOk()) {
        auto loadString = [](const char* key, const char* fallback, char* dest, size_t capacity) {
            const std::string value = storage().getString(key, fallback);
            if (!value.empty()) safeCopy(dest, value.c_str(), capacity);
        };
        loadString("c_ssid", "", activeConfig.wifiSsid, sizeof(activeConfig.wifiSsid));
        loadString("c_pass", "", activeConfig.wifiPass, sizeof(activeConfig.wifiPass));
        loadString("c_srv", "", activeConfig.caldavServer, sizeof(activeConfig.caldavServer));
        loadString("c_usr", "", activeConfig.caldavUser, sizeof(activeConfig.caldavUser));
        loadString("c_pwd", "", activeConfig.caldavPass, sizeof(activeConfig.caldavPass));
        loadString("c_cal", ersa::config::FALLBACK_CALDAV_CALENDAR, activeConfig.caldavCalendar, sizeof(activeConfig.caldavCalendar));
        loadString("c_tod", ersa::config::DEFAULT_CALDAV_TODO, activeConfig.caldavTodoPath, sizeof(activeConfig.caldavTodoPath));
        activeConfig.timezoneOffsetMin = static_cast<int16_t>(storage().getInt("c_tz", activeConfig.timezoneOffsetMin));
        activeConfig.militaryTime = storage().getBool("c_24h", activeConfig.militaryTime);
        activeConfig.fullRefreshInterval = static_cast<uint8_t>(storage().getInt("c_fref", activeConfig.fullRefreshInterval));
        loadString("c_aps", ersa::config::DEFAULT_AP_SSID, activeConfig.apSsid, sizeof(activeConfig.apSsid));
        loadString("c_app", ersa::config::DEFAULT_AP_PASS, activeConfig.apPass, sizeof(activeConfig.apPass));
        activeConfig.apTimeoutSec = static_cast<uint16_t>(storage().getInt("c_apt", activeConfig.apTimeoutSec));
        DebugLog::log("CONFIG loaded (tz=%d, ssid='%s', caldav='%s')",
                      activeConfig.timezoneOffsetMin, activeConfig.wifiSsid, activeConfig.caldavServer);
    } else {
        DebugLog::log("CONFIG no stored preferences; using defaults");
    }
    const uint8_t repaired = sanitizeLoadedConfig();
    if (repaired)
        DebugLog::log("CONFIG validation repaired %u invalid setting(s) with defaults", unsigned(repaired));
}

const Config& get() {
    return activeConfig;
}

void setWifi(const char* ssid, const char* pass) {
    safeCopy(activeConfig.wifiSsid, ssid, sizeof(activeConfig.wifiSsid));
    safeCopy(activeConfig.wifiPass, pass, sizeof(activeConfig.wifiPass));
}

void setCalDav(const char* server, const char* user, const char* pass, const char* calendar, const char* todoPath) {
    safeCopy(activeConfig.caldavServer, server, sizeof(activeConfig.caldavServer));
    safeCopy(activeConfig.caldavUser, user, sizeof(activeConfig.caldavUser));
    safeCopy(activeConfig.caldavPass, pass, sizeof(activeConfig.caldavPass));
    if (calendar && calendar[0] != '\0') {
        safeCopy(activeConfig.caldavCalendar, calendar, sizeof(activeConfig.caldavCalendar));
    }
    if (todoPath && todoPath[0] != '\0') {
        safeCopy(activeConfig.caldavTodoPath, todoPath, sizeof(activeConfig.caldavTodoPath));
    }
}

void setTimezone(int16_t offsetMinutes) {
    activeConfig.timezoneOffsetMin = offsetMinutes;
}

void setTimeFormat(bool military24h) {
    activeConfig.militaryTime = military24h;
}

void setApConfig(const char* ssid, const char* pass, uint16_t timeoutSec) {
    safeCopy(activeConfig.apSsid, ssid, sizeof(activeConfig.apSsid));
    safeCopy(activeConfig.apPass, pass, sizeof(activeConfig.apPass));
    activeConfig.apTimeoutSec = timeoutSec;
}

bool save() {
    auto& store = storage();
    const bool ok = store.setString("c_ssid", activeConfig.wifiSsid) &&
        store.setString("c_pass", activeConfig.wifiPass) &&
        store.setString("c_srv", activeConfig.caldavServer) &&
        store.setString("c_usr", activeConfig.caldavUser) &&
        store.setString("c_pwd", activeConfig.caldavPass) &&
        store.setString("c_cal", activeConfig.caldavCalendar) &&
        store.setString("c_tod", activeConfig.caldavTodoPath) &&
        store.setInt("c_tz", activeConfig.timezoneOffsetMin) &&
        store.setBool("c_24h", activeConfig.militaryTime) &&
        store.setInt("c_fref", activeConfig.fullRefreshInterval) &&
        store.setString("c_aps", activeConfig.apSsid) &&
        store.setString("c_app", activeConfig.apPass) &&
        store.setInt("c_apt", activeConfig.apTimeoutSec);
    if (ok) {
        DebugLog::log("CONFIG saved to NVS");
        return true;
    } else {
        DebugLog::log("CONFIG error committing settings to storage");
        return false;
    }
}

} // namespace WatchConfig
