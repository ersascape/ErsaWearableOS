#pragma once

#include "ersa/common/types.h"
#include <stdint.h>

namespace ersa {
namespace hal {

using BleConnectionCallback = void (*)(bool connected, void* userData);

/// BLE transport, advertising, and link-state contract.
class IBluetooth {
public:
    virtual ~IBluetooth() = default;

    /// Initialize the BLE controller and transport.
    virtual Result<void> init() = 0;
    /// Begin advertising the device's BLE services.
    virtual void startAdvertising() = 0;
    /// Stop BLE advertising.
    virtual void stopAdvertising() = 0;
    // Suspend the radio stack for memory intensive maintenance such as OTA.
    // The link is dropped. Implementations without a suspendable radio may
    // keep their existing behavior and return true.
    /// Release radio resources before memory-intensive maintenance.
    virtual bool suspendForMaintenance() { stopAdvertising(); return true; }
    /// Resume advertising after maintenance completes.
    virtual void resumeAfterMaintenance() { startAdvertising(); }
    /// Return whether a BLE peer is connected.
    virtual bool isConnected() const = 0;
    /// Return whether BLE advertising is active when supported.
    virtual bool isAdvertising() const { return false; }
    // Milliseconds until radio maintenance must run, or UINT32_MAX if none.
    virtual uint32_t nextWakeDelayMs(uint32_t nowMs) const { (void)nowMs; return UINT32_MAX; }
    /// Run bounded transport maintenance from the application task.
    virtual void tick() {}
    /// Return the local BLE device name.
    virtual const char* getDeviceName() const = 0;
    /// Return the local BLE device address.
    virtual const char* getDeviceAddress() const = 0;

    // BLE link state only. Companion data and commands belong to ICompanionSource.
    /// Register or clear the callback for BLE link-state changes.
    virtual void setConnectionCallback(BleConnectionCallback cb, void* userData) = 0;
};

} // namespace hal
} // namespace ersa
