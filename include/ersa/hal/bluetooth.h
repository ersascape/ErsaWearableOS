#pragma once

#include "ersa/common/types.h"
#include <stdint.h>

namespace ersa {
namespace hal {

/** Notification that the BLE link state changed; callback context is caller-owned. */
using BleConnectionCallback = void (*)(bool connected, void* userData);

/**
 * BLE transport, advertising, and link-state contract.
 *
 * This interface owns radio lifecycle only. Companion semantics such as calls,
 * notifications, media, and phone time live behind ICompanionSource so another
 * transport/provider can supply them without pretending to be this BLE stack.
 * Driver callbacks should hand off work and avoid running UI code directly.
 */
class IBluetooth {
public:
    virtual ~IBluetooth() = default;

    /** Initialize the controller and service transport required by the product. */
    virtual Result<void> init() = 0;
    /** Begin or resume advertising; repeated calls should be safe. */
    virtual void startAdvertising() = 0;
    /** Stop advertising without necessarily tearing down an established link. */
    virtual void stopAdvertising() = 0;
    // Suspend the radio stack for memory intensive maintenance such as OTA.
    // The link is dropped. Implementations without a suspendable radio may
    // keep their existing behavior and return true.
    /// Release radio resources before memory-intensive maintenance.
    virtual bool suspendForMaintenance() { stopAdvertising(); return true; }
    /// Resume advertising after maintenance completes.
    virtual void resumeAfterMaintenance() { startAdvertising(); }
    /** Return current link state; this does not imply companion services are ready. */
    virtual bool isConnected() const = 0;
    /** Return advertising state when the backend can report it, else false. */
    virtual bool isAdvertising() const { return false; }
    /**
     * Return delay until the next required BLE maintenance deadline.
     * UINT32_MAX means there is no scheduled deadline; sleep policy uses this
     * value to choose a safe wait duration without polling continuously.
     */
    virtual uint32_t nextWakeDelayMs(uint32_t nowMs) const { (void)nowMs; return UINT32_MAX; }
    /** Run bounded maintenance deferred from radio callbacks. */
    virtual void tick() {}
    /// Return the local BLE device name.
    virtual const char* getDeviceName() const = 0;
    /** Return a stable formatted address string owned by the backend. */
    virtual const char* getDeviceAddress() const = 0;

    /**
     * Register the single BLE link-state callback; null clears it.
     * This reports transport only. Use ICompanionSource for provider/session
     * readiness because encrypted link-up can precede service discovery.
     */
    virtual void setConnectionCallback(BleConnectionCallback cb, void* userData) = 0;
};

} // namespace hal
} // namespace ersa
