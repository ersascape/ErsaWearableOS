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
    NetworkHandle();
    explicit NetworkHandle(NetworkManager* mgr);
    ~NetworkHandle();

    NetworkHandle(const NetworkHandle&) = delete;
    NetworkHandle& operator=(const NetworkHandle&) = delete;

    NetworkHandle(NetworkHandle&& other) noexcept;
    NetworkHandle& operator=(NetworkHandle&& other) noexcept;

    bool isValid() const { return active_; }
    void release();

private:
    NetworkManager* mgr_{nullptr};
    bool active_{false};
};

class NetworkManager {
public:
    explicit NetworkManager(events::EventBus& bus = events::EventBus::instance(),
                            hal::IWifiRadio* wifi = nullptr);

    Result<void> init();
    void tick(uint32_t currentUptimeMs);

    NetworkHandle requestInternet();
    void acquire();
    void release();

    bool isConnected() const;
    bool isConnecting() const;
    size_t getActiveHandleCount() const;

    static NetworkManager& instance();
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
