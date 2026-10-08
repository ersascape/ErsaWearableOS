#pragma once

#include "ersa/hal/companion_source.h"

#if defined(ARDUINO)
#include <Arduino.h>
#include <esp_gatts_api.h>

namespace ersa {
namespace hal {

/**
 * ESP32 GATT client for Apple Notification, Media, and Current Time services.
 *
 * This is a vendor transport adapter used by Esp32Bluetooth. It owns GATT
 * discovery, descriptors, authentication-dependent subscriptions, and wire
 * commands, then emits semantic callbacks through CompanionSource function
 * types. The pImpl keeps ESP-IDF GATT internals out of unrelated headers.
 */
class Esp32AppleClient {
public:
    /** Create a client shell; startDiscovery() begins a peer session. */
    Esp32AppleClient();
    /** Stop an active session and release the GATT implementation. */
    ~Esp32AppleClient();

    /** Set or clear the normalized call-event sink. */
    void setCallCallback(CompanionCallCallback cb, void* userData);
    /** Set or clear the normalized media-event sink. */
    void setMediaCallback(CompanionMediaCallback cb, void* userData);
    /** Set or clear the normalized notification-event sink. */
    void setNotificationCallback(CompanionNotificationCallback cb, void* userData);
    /** Set or clear the normalized Current Time sink. */
    void setTimeCallback(CompanionTimeCallback cb, void* userData);

    /** Discover supported Apple services for the authenticated BLE peer. */
    void startDiscovery(const esp_bd_addr_t bda, esp_ble_addr_type_t addrType = BLE_ADDR_TYPE_RANDOM);
    /** Resume service discovery only after link encryption/authentication succeeds. */
    void authenticationComplete(bool success);
    /** Cancel discovery, notifications, and outstanding work for this peer. */
    void stop();
    /** Pause workers/GATT activity for OTA and wait up to the given deadline. */
    bool suspendForMaintenance(uint32_t timeoutMs);
    /** Forget vendor GATT objects before BLEDevice deinitializes their stack. */
    void prepareForStackRestart();

    bool isAncsActive() const;
    bool isAmsActive() const;
    bool isCtsActive() const;

    /** Send the ANCS positive call action when currently advertised as valid. */
    void acceptCall();
    /** Send the ANCS negative call action when currently advertised as valid. */
    void rejectCall();
    /** Dismiss one notification UID when the peer advertises dismissal support. */
    bool dismissNotification(uint32_t uid);
    /** Send a supported AMS remote-command action to the peer. */
    void mediaCommand(CompanionMediaAction action);

private:
    class Impl;
    Impl* pImpl_{nullptr};
};

} // namespace hal
} // namespace ersa

#else

namespace ersa {
namespace hal {

class Esp32AppleClient {
public:
    Esp32AppleClient() = default;
    ~Esp32AppleClient() = default;
    void setCallCallback(CompanionCallCallback, void*) {}
    void setMediaCallback(CompanionMediaCallback, void*) {}
    void setNotificationCallback(CompanionNotificationCallback, void*) {}
    void setTimeCallback(CompanionTimeCallback, void*) {}
    void startDiscovery(const uint8_t*, uint8_t = 0) {}
    void authenticationComplete(bool) {}
    void stop() {}
    bool suspendForMaintenance(uint32_t) { return true; }
    void prepareForStackRestart() {}
    bool isAncsActive() const { return false; }
    bool isAmsActive() const { return false; }
    bool isCtsActive() const { return false; }
    void acceptCall() {}
    void rejectCall() {}
    bool dismissNotification(uint32_t) { return false; }
    void mediaCommand(CompanionMediaAction) {}
};

} // namespace hal
} // namespace ersa

#endif
