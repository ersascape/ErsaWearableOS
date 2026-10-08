#pragma once

#include "ersa/common/types.h"
#include "ersa/events/event_bus.h"
#include "ersa/hal/wifi.h"
#include <stdint.h>
#include <stddef.h>

namespace ersa {
namespace services {

class NetworkManager;

class NetworkHandle {
public:
    /** Construct an empty handle that holds no radio lease. */
    NetworkHandle();
    /** Acquire one manager lease; move semantics ensure exactly-once release. */
    explicit NetworkHandle(NetworkManager* mgr);
    /** Release the held lease during scope cleanup. */
    ~NetworkHandle();

    NetworkHandle(const NetworkHandle&) = delete;
    NetworkHandle& operator=(const NetworkHandle&) = delete;

    /** Transfer a live lease without changing the manager's reference count. */
    NetworkHandle(NetworkHandle&& other) noexcept;
    /** Release this lease, then take ownership of the source lease. */
    NetworkHandle& operator=(NetworkHandle&& other) noexcept;

    /// Return whether this handle currently owns a network lease.
    bool isValid() const { return active_; }
    /// Release this handle's network lease.
    void release();

private:
    NetworkManager* mgr_{nullptr};
    bool active_{false};
};

/**
 * Coordinates Wi-Fi availability with independent service clients.
 *
 * Each NetworkHandle is a scoped radio-use lease. The manager keeps the radio
 * active while at least one lease exists and powers it down after the final
 * lease and idle policy permit. This makes resource lifetime explicit and
 * avoids one app disconnecting Wi-Fi while another service still needs it.
 */
class NetworkManager {
public:
    /** Bind a radio and event bus; null radio provides a no-hardware host mode. */
    explicit NetworkManager(events::EventBus& bus = events::EventBus::instance(),
                            hal::IWifiRadio* wifi = nullptr);

    /** Subscribe to link transitions and initialize radio state if available. */
    Result<void> init();
    /** Apply deferred link events and idle power-off using monotonic uptime. */
    void tick(uint32_t currentUptimeMs);

    /** Acquire an RAII lease; connectivity remains active until release. */
    NetworkHandle requestInternet();
    /** Increment an unscoped reference count for legacy/manual lifetime code. */
    void acquire();
    /** Decrement a prior raw acquisition; unmatched releases are ignored. */
    void release();

    /// Return whether station networking is connected.
    bool isConnected() const;
    /// Return whether a station connection is being established.
    bool isConnecting() const;
    /// Return the number of active network leases.
    size_t getActiveHandleCount() const;

    /// Return the installed process-wide network manager.
    static NetworkManager& instance();
    /// Install the process-wide network manager.
    static void setInstance(NetworkManager* instance);

private:
    events::EventBus& bus_;
    hal::IWifiRadio* wifi_;
    size_t activeHandles_{0};
    bool connected_{false};
    bool connecting_{false};
    uint32_t lastActivityMs_{0};
};

} // namespace services
} // namespace ersa
