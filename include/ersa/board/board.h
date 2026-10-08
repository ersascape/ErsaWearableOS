#pragma once

#include "ersa/common/types.h"
#include "ersa/board/board_config.h"
#include "ersa/board/pins.h"
#include "ersa/hal/display.h"
#include "ersa/hal/rtc.h"
#include "ersa/hal/battery.h"
#include "ersa/hal/input.h"
#include "ersa/hal/bluetooth.h"
#include "ersa/hal/companion_source.h"
#include "ersa/hal/wifi.h"
#include "ersa/hal/power_management.h"
#include "ersa/hal/console.h"
#include "ersa/hal/http_server.h"

namespace ersa {
namespace board {

struct DeviceInfo {
    /** User-facing product name, kept separate from implementation codename. */
    const char* name;
    /** Stable product identifier used by firmware packaging and diagnostics. */
    const char* codename;
    /** Manufacturer identifier for organizing related board products. */
    const char* manufacturer;
};

/**
 * Composition contract for one selected board support package.
 *
 * A BSP owns concrete driver instances and exposes them through generic HAL
 * references. Firmware services depend on this contract instead of naming a
 * specific watch or panel, allowing another board composition to replace the
 * current one without changing application/service code. Returned references
 * are borrowed and remain valid for the lifetime of the board object.
 */
class Board {
public:
    virtual ~Board() = default;

    /** Configure pinctrl, buses, and board-owned resources before service init. */
    virtual Result<void> init() = 0;
    /** Return the stable user-facing product label. */
    virtual const char* getName() const = 0;
    /** Return static manufacturer/platform/product identity for diagnostics. */
    virtual const DeviceInfo& getDeviceInfo() const = 0;
    /** Return immutable capabilities and geometry used by generic policy. */
    virtual const BoardConfig& getConfig() const = 0;
    /** Return the logical pin contract used by pinctrl and peripheral setup. */
    virtual const Pins& getPins() const = 0;

    /// Return the capability set declared by this board.
    const BoardCapabilities& capabilities() const {
        return getConfig().capabilities;
    }

    /// Return the generic display implementation selected by this BSP.
    virtual hal::IDisplay& getDisplay() = 0;
    /// Return the RTC implementation selected by this BSP.
    virtual hal::IRtc& getRtc() = 0;
    /// Return the battery implementation selected by this BSP.
    virtual hal::IBattery& getBattery() = 0;
    /// Return the normalized input implementation selected by this BSP.
    virtual hal::IInput& getInput() = 0;
    /// Return the BLE transport selected by this BSP.
    virtual hal::IBluetooth& getBluetooth() = 0;
    /// Return the companion-data provider selected by this BSP.
    virtual hal::ICompanionSource& getCompanionSource() = 0;
    /// Return the Wi-Fi radio selected by this BSP.
    virtual hal::IWifiRadio& getWifi() = 0;
    /// Return the platform power-management implementation selected by this BSP.
    virtual hal::IPowerManagement& getPowerManagement() = 0;
    /// Return the platform debug/control console selected by this BSP.
    virtual hal::IConsole& getConsole() = 0;
    /// Return the board's local HTTP server adapter used by captive apps.
    virtual hal::IHttpServer& getHttpServer() = 0;
    /// Return the board's captive DNS responder adapter.
    virtual hal::IDnsServer& getDnsServer() = 0;

    /** Return monotonic milliseconds; values wrap according to uint32_t uptime. */
    virtual uint32_t getUptimeMs() const = 0;
    /** Delay the caller task; do not use this for UI-loop waits on long I/O. */
    virtual void delayMs(uint32_t ms) = 0;
    /** Request a platform restart, normally used for reset or OTA handoff. */
    virtual void restart() = 0;

    /**
     * Return the build-selected board before initialization.
     * Kconfig chooses the composition at compile time, avoiding a runtime
     * factory and keeping invalid board/firmware combinations unrepresentable.
     */
    static Board& current();
    /**
     * Return the selected board when one is installed, or null when host tests
     * deliberately build without an embedded BSP provider.
     */
    static Board* currentOrNull();
};

} // namespace board
} // namespace ersa
