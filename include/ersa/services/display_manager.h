#pragma once

#include "ersa/common/types.h"
#include "ersa/hal/display.h"
#include <stdint.h>

namespace ersa {
namespace services {

/**
 * Coalesces drawing changes and applies panel-specific refresh/power policy.
 *
 * Apps only mark content dirty; this manager rate-limits physical refreshes,
 * tracks partial-frame counts, and powers the panel down after inactivity.
 * Keeping those decisions here prevents fast UI events from causing excessive
 * e-paper flashing or shortening panel lifetime.
 */
class DisplayManager {
public:
    /** Minimum delay between refresh submissions, protecting panel timing. */
    static constexpr uint32_t MIN_REFRESH_INTERVAL_MS = 500;
    /** Idle duration before the display controller may power off. */
    static constexpr uint32_t IDLE_POWEROFF_TIMEOUT_MS = 8000;
    /** Partial frames before policy requests a full ghost-clearing waveform. */
    static constexpr uint16_t FULL_REFRESH_FRAME_COUNT = 360;

    /** Bind the generic display; manager does not own the driver object. */
    explicit DisplayManager(hal::IDisplay& display);

    /** Initialize the panel and seed refresh/idle bookkeeping. */
    Result<void> init();
    /** Apply due refresh and idle-power transitions for the current uptime. */
    void tick(uint32_t currentUptimeMs);

    /**
     * Record invalidated framebuffer content; repeated marks coalesce into one
     * future refresh. A full request is sticky until serviced.
     */
    void markDirty(bool fullRefresh = false);
    /// Return whether the panel needs a refresh.
    bool isDirty() const;

    // Trigger a refresh if dirty and interval has elapsed
    /**
     * Submit one pending update only after the minimum refresh interval.
     * @return True when a panel refresh was submitted this call.
     */
    bool updateIfDirty(uint32_t currentUptimeMs);

    // Explicit force refresh
    /** Bypass dirty scheduling for explicit refresh requests, such as redraw. */
    void refresh(bool full = false, uint32_t currentUptimeMs = 0);

    /**
     * Refresh an invalidated region and centralize full-waveform cadence.
     * @param bounds Changed framebuffer area; ignored for a full waveform.
     * @param forceFull Request a full waveform for first frame or known day change.
     * @param currentUptimeMs Monotonic time used to update panel bookkeeping.
     */
    void refreshRect(const Rect& bounds, bool forceFull, uint32_t currentUptimeMs);

    // User activity notification (keeps panel powered)
    /** Reset the idle timer so interaction is not interrupted by power-off. */
    void noteActivity(uint32_t currentUptimeMs);

    /** Read the ghosting-policy counter used to decide when to full-refresh. */
    uint16_t getPartialFrameCount() const;
    /// Reset the partial-frame refresh counter.
    void resetPartialFrameCount();

    /** Return the borrowed display HAL so coordinated services share one panel. */
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
    uint16_t partialFrames_{0};
    uint32_t lastRefreshTime_{0};
    uint32_t lastActivityTime_{0};
};

} // namespace services
} // namespace ersa
