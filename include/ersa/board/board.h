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

namespace ersa {
namespace board {

struct DeviceInfo {
    const char* name;
    const char* codename;
    const char* manufacturer;
};

class Board {
public:
    virtual ~Board() = default;

    virtual Result<void> init() = 0;
    virtual const char* getName() const = 0;
    virtual const DeviceInfo& getDeviceInfo() const = 0;
    virtual const BoardConfig& getConfig() const = 0;
    virtual const Pins& getPins() const = 0;

    const BoardCapabilities& capabilities() const {
        return getConfig().capabilities;
    }

    virtual hal::IDisplay& getDisplay() = 0;
    virtual hal::IRtc& getRtc() = 0;
    virtual hal::IBattery& getBattery() = 0;
    virtual hal::IInput& getInput() = 0;
    virtual hal::IBluetooth& getBluetooth() = 0;
    virtual hal::ICompanionSource& getCompanionSource() = 0;
    virtual hal::IWifiRadio& getWifi() = 0;

    virtual uint32_t getUptimeMs() const = 0;
    virtual void delayMs(uint32_t ms) = 0;
    virtual void restart() = 0;

    static Board& current();
    static Board* currentOrNull();
    static void setCurrent(Board* board);
};

} // namespace board
} // namespace ersa
