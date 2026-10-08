#pragma once

#include "ersa/common/types.h"
#include <string>
#include <stdint.h>

namespace ersa {
namespace services {

/// Typed user settings backed by persistent key/value storage.
class SettingsService {
public:
    virtual ~SettingsService() = default;

    /// Load settings or initialize documented defaults.
    virtual Result<void> init() = 0;

    /// Return the configured Wi-Fi network name.
    virtual std::string getWifiSsid() const = 0;
    /// Set the Wi-Fi network name.
    virtual void setWifiSsid(const std::string& ssid) = 0;

    /// Return the configured Wi-Fi password.
    virtual std::string getWifiPass() const = 0;
    /// Set the Wi-Fi password.
    virtual void setWifiPass(const std::string& pass) = 0;

    /// Return the timezone offset in minutes from UTC.
    virtual int16_t getTimezoneOffsetMin() const = 0;
    /// Set the timezone offset in minutes from UTC.
    virtual void setTimezoneOffsetMin(int16_t offset) = 0;

    /// Return whether the user selected 24-hour time display.
    virtual bool isMilitaryTime() const = 0;
    /// Set whether time should use a 24-hour display.
    virtual void setMilitaryTime(bool military) = 0;

    /// Persist pending settings changes.
    virtual void save() = 0;

    /// Return the installed process-wide settings service.
    static SettingsService& instance();
    /// Install the process-wide settings service.
    static void setInstance(SettingsService* instance);
};

} // namespace services
} // namespace ersa
