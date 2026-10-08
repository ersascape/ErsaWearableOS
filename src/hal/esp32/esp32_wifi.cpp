#if defined(ARDUINO)

#include "hal/esp32/esp32_wifi.h"
#include <WiFi.h>
#include <stdio.h>

namespace ersa {
namespace hal {

Result<void> Esp32Wifi::init() {
    WiFi.mode(WIFI_OFF);
    return Result<void>();
}

void Esp32Wifi::enableStation() { WiFi.mode(WIFI_STA); }

void Esp32Wifi::connectStation(const char* ssid, const char* password, bool powerSave) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(powerSave);
    WiFi.begin(ssid ? ssid : "", password ? password : "");
}

WifiState Esp32Wifi::state() const {
    const wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_OFF) return WifiState::Off;
    if (mode == WIFI_AP || mode == WIFI_AP_STA) return WifiState::AccessPoint;
    if (WiFi.status() == WL_CONNECTED) return WifiState::Connected;
    return mode == WIFI_STA ? WifiState::Connecting : WifiState::Disconnected;
}

bool Esp32Wifi::copyLocalAddress(char* output, size_t capacity) const {
    if (!output || capacity == 0 || WiFi.status() != WL_CONNECTED) return false;
    const IPAddress address = WiFi.localIP();
    const int written = snprintf(output, capacity, "%u.%u.%u.%u",
                                 address[0], address[1], address[2], address[3]);
    if (written < 0 || static_cast<size_t>(written) >= capacity) return false;
    return true;
}

Result<void> Esp32Wifi::startAccessPoint(const char* ssid, const char* password) {
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(ssid ? ssid : "Ersa", password && password[0] ? password : nullptr))
        return Result<void>(ErrorCode::HardwareFault, "Failed to start Wi-Fi access point");
    return Result<void>();
}

void Esp32Wifi::stopAccessPoint() { WiFi.softAPdisconnect(true); }

bool Esp32Wifi::copyAccessPointAddress(char* output, size_t capacity) const {
    if (!output || capacity == 0) return false;
    const IPAddress address = WiFi.softAPIP();
    const int written = snprintf(output, capacity, "%u.%u.%u.%u",
                                 address[0], address[1], address[2], address[3]);
    if (written < 0 || static_cast<size_t>(written) >= capacity) return false;
    return true;
}

void Esp32Wifi::disconnect(bool powerOff) {
    WiFi.disconnect(true);
    if (powerOff) WiFi.mode(WIFI_OFF);
}

} // namespace hal
} // namespace ersa

#endif // ARDUINO
