#pragma once

#include "ersa/common/types.h"
#include "ersa/hal/display.h"
#include <stdint.h>

namespace ersa {
namespace services {

/// Coalesces UI invalidations and schedules safe panel refresh/power transitions.
class DisplayManager {
public:
    static constexpr uint32_t MIN_REFRESH_INTERVAL_MS = 500;
    static constexpr uint32_t IDLE_POWEROFF_TIMEOUT_MS = 8000;
    static constexpr uint8_t FULL_REFRESH_FRAME_COUNT = 25;

    /// Manage the supplied display contract.
    explicit DisplayManager(hal::IDisplay& display);

    /// Initialize the display and manager state.
    Result<void> init();
    /// Advance refresh and idle-power policy using current uptime.
    void tick(uint32_t currentUptimeMs);

    /// Mark buffered content as changed and optionally request a full refresh.
    void markDirty(bool fullRefresh = false);
    /// Return whether the panel needs a refresh.
    bool isDirty() const;

    // Trigger a refresh if dirty and interval has elapsed
    /// Refresh pending content when the minimum interval allows it.
    bool updateIfDirty(uint32_t currentUptimeMs);

    // Explicit force refresh
    /// Force a panel refresh, optionally using the full waveform.
    void refresh(bool full = false, uint32_t currentUptimeMs = 0);

    // User activity notification (keeps panel powered)
    /// Record user activity to keep the panel powered through interaction.
    void noteActivity(uint32_t currentUptimeMs);

    /// Return the number of partial frames since the last full refresh.
    uint8_t getPartialFrameCount() const;
    /// Reset the partial-frame refresh counter.
    void resetPartialFrameCount();

    /// Return the display contract managed by this service.
    hal::IDisplay& getDisplay();

    /// Return the installed process-wide display manager.
    static DisplayManager& instance();
    /// Install a process-wide display manager for legacy service accessors.
    static void setInstance(DisplayManager* instance);

private:
    hal::IDisplay& display_;
    bool dirty_{true};
    bool fullNeeded_{true};
    bool panelPowered_{false};
    uint8_t partialFrames_{0};
    uint32_t lastRefreshTime_{0};
    uint32_t lastActivityTime_{0};
};

} // namespace services
} // namespace ersa
