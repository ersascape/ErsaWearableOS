#pragma once

#include "ersa/common/types.h"
#include <string>
#include <stdint.h>

namespace ersa {
namespace services {

/**
 * Typed user preferences exposed independently from persistent storage format.
 *
 * UI and service code use named settings rather than raw keys so defaults,
 * validation, and future migrations remain centralized in the implementation.
 * Setters update the service's in-memory view; save() is the explicit durable
 * commit boundary for backends that buffer writes.
 */
class SettingsService {
public:
    virtual ~SettingsService() = default;

    /** Load persisted values, validate ranges, and fill absent keys with defaults. */
    virtual Result<void> init() = 0;

    /// Return the configured Wi-Fi network name.
    virtual std::string getWifiSsid() const = 0;
    /** Update the pending SSID; call save() to persist it. */
    virtual void setWifiSsid(const std::string& ssid) = 0;

    /// Return the configured Wi-Fi password.
    virtual std::string getWifiPass() const = 0;
    /** Update the pending passphrase; implementations must not log its value. */
    virtual void setWifiPass(const std::string& pass) = 0;

    /// Return the timezone offset in minutes from UTC.
    virtual int16_t getTimezoneOffsetMin() const = 0;
    /** Update offset minutes (east positive); persist with save(). */
    virtual void setTimezoneOffsetMin(int16_t offset) = 0;

    /// Return whether the user selected 24-hour time display.
    virtual bool isMilitaryTime() const = 0;
    /** Update the display preference; formatting remains the view's responsibility. */
    virtual void setMilitaryTime(bool military) = 0;

    /** Commit pending setting changes to durable storage. */
    virtual void save() = 0;

    /// Return the installed process-wide settings service.
    static SettingsService& instance();
    /// Install the process-wide settings service.
    static void setInstance(SettingsService* instance);
};

} // namespace services
} // namespace ersa
