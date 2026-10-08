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
    /// Create an inactive handle.
    NetworkHandle();
    /// Create an active handle and acquire a network wake lease.
    explicit NetworkHandle(NetworkManager* mgr);
    /// Release the network wake lease if still active.
    ~NetworkHandle();

    NetworkHandle(const NetworkHandle&) = delete;
    NetworkHandle& operator=(const NetworkHandle&) = delete;

    NetworkHandle(NetworkHandle&& other) noexcept;
    NetworkHandle& operator=(NetworkHandle&& other) noexcept;

    /// Return whether this handle currently owns a network lease.
    bool isValid() const { return active_; }
    /// Release this handle's network lease.
    void release();

private:
    NetworkManager* mgr_{nullptr};
    bool active_{false};
};

/// Coordinates Wi-Fi radio availability for foreground network operations.
class NetworkManager {
public:
    /// Manage a radio and event bus; a null radio keeps host use hardware-free.
    explicit NetworkManager(events::EventBus& bus = events::EventBus::instance(),
                            hal::IWifiRadio* wifi = nullptr);

    /// Initialize network event handling and radio state.
    Result<void> init();
    /// Advance connection and radio-idle policy.
    void tick(uint32_t currentUptimeMs);

    /// Acquire a scoped lease that keeps station connectivity available.
    NetworkHandle requestInternet();
    /// Acquire an unscoped network lease for legacy call sites.
    void acquire();
    /// Release an unscoped network lease.
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
