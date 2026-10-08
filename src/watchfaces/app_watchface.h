#pragma once

#include "ersa/app/application.h"
#include <stdint.h>

namespace ersa {
namespace watchface {

/**
 * Default clock face showing local time, date, and companion status.
 * It observes normalized app events and requests only the dynamic region for
 * ordinary minute/media/call changes; the fixed footer is preserved to avoid
 * unnecessary e-paper refresh and ghosting.
 */
class AppWatchface : public app::Application {
public:
    /** Initialize cached render keys so first paint includes every field. */
    AppWatchface();
    ~AppWatchface() override = default;

    /** Return the stable registry ID used for watchface navigation. */
    const char* getId() const override { return "watchface_clock"; }
    /** Return the product-facing watchface label. */
    const char* getTitle() const override { return "Watchface"; }

    /** Mark the clock face active and request its initial render. */
    void onEnter() override;
    /** Stop face-specific observation when navigating to another app. */
    void onExit() override;
    /** Update cached display state for time, media, call, or notification events. */
    void onEvent(const events::Event& event) override;
    /** Draw the clock and dynamic companion information using display HAL calls. */
    void render(hal::IDisplay& display, bool fullRefresh) override;

    // Clock/media/call content changes above the fixed weekday/date footer.
    // Midnight already forces a full refresh in WatchUi.
    Rect getPartialBounds() const override { return Rect{0, 0, 200, 160}; }

    /** Return the single static instance registered by the app registry. */
    static AppWatchface& instance();

private:
    uint32_t shownMinute_{UINT32_MAX};
    uint8_t shownDay_{0};
    bool shownRtcHealthy_{false};
};

} // namespace watchface
} // namespace ersa
