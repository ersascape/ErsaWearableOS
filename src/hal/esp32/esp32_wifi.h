#pragma once

#if defined(ARDUINO)

#include "ersa/hal/wifi.h"

namespace ersa {
namespace hal {

class Esp32Wifi final : public IWifiRadio {
public:
    Result<void> init() override;
    void enableStation() override;
    void connectStation(const char* ssid, const char* password, bool powerSave) override;
    WifiState state() const override;
    bool copyLocalAddress(char* output, size_t capacity) const override;
    Result<void> startAccessPoint(const char* ssid, const char* password) override;
    void stopAccessPoint() override;
    bool copyAccessPointAddress(char* output, size_t capacity) const override;
    void disconnect(bool powerOff) override;
};

} // namespace hal
} // namespace ersa

#endif // ARDUINO
