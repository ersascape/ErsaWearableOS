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

namespace ersa {
namespace board {

struct DeviceInfo {
    const char* name;
    const char* codename;
    const char* manufacturer;
};

/// Composition contract for one selected board support package.
class Board {
public:
    virtual ~Board() = default;

    /// Initialize board pin control and board-owned platform resources.
    virtual Result<void> init() = 0;
    /// Return the product name shown to users.
    virtual const char* getName() const = 0;
    /// Return stable board identity metadata.
    virtual const DeviceInfo& getDeviceInfo() const = 0;
    /// Return board capabilities and peripheral characteristics.
    virtual const BoardConfig& getConfig() const = 0;
    /// Return this board's logical pin assignments.
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

    /// Return milliseconds since system startup.
    virtual uint32_t getUptimeMs() const = 0;
    /// Block the current task for the requested duration.
    virtual void delayMs(uint32_t ms) = 0;
    /// Restart the device.
    virtual void restart() = 0;

    /// Return the Kconfig-selected board contract, available before device init.
    static Board& current();
    /// Return the selected board, or null in host tests without a BSP provider.
    static Board* currentOrNull();
};

} // namespace board
} // namespace ersa
