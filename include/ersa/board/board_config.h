#pragma once

#include <stdint.h>

namespace ersa {
namespace board {

/** Optional hardware features present on a product, independent of their drivers. */
struct BoardCapabilities {
    /** Product includes a Wi-Fi-capable radio. */
    bool wifi{false};
    /** Product includes a Bluetooth-capable radio. */
    bool bluetooth{false};
    /** Product has a real-time clock source. */
    bool rtc{false};
    /** Product has a dedicated battery gauge, not only a voltage divider. */
    bool batteryGauge{false};
    /** Product includes a haptic actuator. */
    bool haptics{false};
    /** Product includes a touch input surface. */
    bool touch{false};
    /** Product exposes physical buttons. */
    bool buttons{false};
};

/** Display properties used by layout and refresh policy. */
struct DisplayConfig {
    /** Logical display width presented to app drawing code. */
    uint16_t width{200};
    /** Logical display height presented to app drawing code. */
    uint16_t height{200};
    /** Whether the driver can refresh a bounded sub-rectangle. */
    bool partialRefresh{true};
    /** Whether the panel uses e-paper refresh and ghosting behavior. */
    bool isEpaper{true};
};

/**
 * Product-level configuration assembled by one board support package.
 * This holds capabilities and geometry, not pin numbers or driver objects.
 */
struct BoardConfig {
    /** Human-readable board/product name. */
    const char* name{"Generic Board"};
    /** Optional hardware inventory available to feature policy. */
    BoardCapabilities capabilities;
    /** Display dimensions and refresh characteristics. */
    DisplayConfig display;
};

} // namespace board
} // namespace ersa
