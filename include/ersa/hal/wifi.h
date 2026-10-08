#pragma once

#include "ersa/common/types.h"
#include <stddef.h>

namespace ersa {
namespace hal {

enum class WifiState { Off, Disconnected, Connecting, Connected, AccessPoint };

// Wi-Fi radio and link control. Socket and HTTP protocols sit above this HAL.
/// Wi-Fi station and access-point radio control; networking protocols live above this interface.
class IWifiRadio {
public:
    virtual ~IWifiRadio() = default;
    /// Initialize the radio backend.
    virtual Result<void> init() = 0;
    /// Enable station mode.
    virtual void enableStation() = 0;
    /// Connect to a station network, optionally enabling radio power save.
    virtual void connectStation(const char* ssid, const char* password, bool powerSave) = 0;
    /// Return the current station/radio state.
    virtual WifiState state() const = 0;
    /// Copy the local station address into a caller-owned buffer.
    virtual bool copyLocalAddress(char* output, size_t capacity) const = 0;
    /// Start an access point using the supplied credentials.
    virtual Result<void> startAccessPoint(const char* ssid, const char* password) = 0;
    /// Stop access point mode.
    virtual void stopAccessPoint() = 0;
    /// Copy the access point address into a caller-owned buffer.
    virtual bool copyAccessPointAddress(char* output, size_t capacity) const = 0;
    /// Disconnect from the current network and optionally power off the radio.
    virtual void disconnect(bool powerOff) = 0;
};

} // namespace hal
} // namespace ersa
