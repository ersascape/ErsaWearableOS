#pragma once

#include "ersa/common/types.h"
#include <stddef.h>

namespace ersa {
namespace hal {

enum class WifiState { Off, Disconnected, Connecting, Connected, AccessPoint };

// Wi-Fi radio and link control. Socket and HTTP protocols sit above this HAL.
class IWifiRadio {
public:
    virtual ~IWifiRadio() = default;
    virtual Result<void> init() = 0;
    virtual void enableStation() = 0;
    virtual void connectStation(const char* ssid, const char* password, bool powerSave) = 0;
    virtual WifiState state() const = 0;
    virtual bool copyLocalAddress(char* output, size_t capacity) const = 0;
    virtual Result<void> startAccessPoint(const char* ssid, const char* password) = 0;
    virtual void stopAccessPoint() = 0;
    virtual bool copyAccessPointAddress(char* output, size_t capacity) const = 0;
    virtual void disconnect(bool powerOff) = 0;
};

} // namespace hal
} // namespace ersa
