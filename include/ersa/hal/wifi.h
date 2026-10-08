#pragma once

#include "ersa/common/types.h"
#include <stddef.h>

namespace ersa {
namespace hal {

/** Radio state shared by station and access-point lifecycle code. */
enum class WifiState { Off, Disconnected, Connecting, Connected, AccessPoint };

// Wi-Fi radio and link control. Socket and HTTP protocols sit above this HAL.
/**
 * Wi-Fi station and access-point radio control; networking protocols live above this interface.
 *
 * This HAL deliberately exposes link control and addresses only. HTTP, NTP,
 * CalDAV, and other sockets belong to services, which lets those protocols be
 * tested without the vendor Wi-Fi stack and prevents board pin details leaking
 * into application code.
 */
class IWifiRadio {
public:
    virtual ~IWifiRadio() = default;
    /** Initialize the controller and release it to a known Off/Disconnected state. */
    virtual Result<void> init() = 0;
    /** Configure station mode before issuing a connection request. */
    virtual void enableStation() = 0;
    /**
     * Start station association with provided credentials.
     * @param ssid Null-terminated network name.
     * @param password Null-terminated passphrase, or empty for an open network.
     * @param powerSave Keep radio modem sleep enabled when coexisting with BLE.
     * Completion is asynchronous; poll state() instead of blocking this call.
     */
    virtual void connectStation(const char* ssid, const char* password, bool powerSave) = 0;
    /// Return the current station/radio state.
    virtual WifiState state() const = 0;
    /** Copy the station IPv4 address to caller storage; false means unavailable or truncated. */
    virtual bool copyLocalAddress(char* output, size_t capacity) const = 0;
    /**
     * Start a local configuration AP.
     * @return Error when credentials, radio state, or driver setup prevents AP startup.
     */
    virtual Result<void> startAccessPoint(const char* ssid, const char* password) = 0;
    /// Stop access point mode.
    virtual void stopAccessPoint() = 0;
    /// Copy the access point address into a caller-owned buffer.
    virtual bool copyAccessPointAddress(char* output, size_t capacity) const = 0;
    /** Disconnect the active mode and optionally turn the radio off to save power. */
    virtual void disconnect(bool powerOff) = 0;
};

} // namespace hal
} // namespace ersa
