#pragma once

#if defined(ARDUINO)

#include "ersa/board/board.h"
#include "ersa/board/board_config.h"
#include "drivers/display/gxepd2/gxepd2_display.h"
#include "hal/esp32/esp32_rtc.h"
#include "hal/esp32/esp32_battery.h"
#include "hal/esp32/esp32_input.h"
#include "hal/esp32/esp32_bluetooth.h"
#include "hal/esp32/esp32_wifi.h"

namespace ersa {
namespace board {

class TerraPins final : public Pins {
public:
    const I2cPins& i2c() const override { return i2c_; }
    const SpiPins& spi() const override { return spi_; }
    const DisplayPins& display() const override { return display_; }
    const ButtonPins& buttons() const override { return buttons_; }
    const BatteryPins& battery() const override { return battery_; }

private:
    const I2cPins i2c_{{6, PinFunction::I2c}, {7, PinFunction::I2c}};
    const SpiPins spi_{{8, PinFunction::Spi}, {10, PinFunction::Spi}, {-1, PinFunction::Spi}};
    const DisplayPins display_{{5, PinFunction::Gpio, PinPull::None, PinActiveLevel::Low, PinDirection::Output},
                               {20, PinFunction::Gpio, PinPull::None, PinActiveLevel::High, PinDirection::Output},
                               {21, PinFunction::Gpio, PinPull::None, PinActiveLevel::Low, PinDirection::Output},
                               {9, PinFunction::Gpio, PinPull::None, PinActiveLevel::High, PinDirection::Input}};
    const ButtonPins buttons_{{4, PinFunction::Gpio, PinPull::Up, PinActiveLevel::Low, PinDirection::Input},
                              {3, PinFunction::Gpio, PinPull::Up, PinActiveLevel::Low, PinDirection::Input}};
    const BatteryPins battery_{{2, PinFunction::Adc, PinPull::None, PinActiveLevel::High, PinDirection::Input}};
};

class BoardTerra : public Board {
public:
    BoardTerra();
    ~BoardTerra() override = default;

    Result<void> init() override;
    const char* getName() const override { return getDeviceInfo().name; }
    const DeviceInfo& getDeviceInfo() const override;
    const BoardConfig& getConfig() const override { return config_; }
    const Pins& getPins() const override { return pins_; }

    hal::IDisplay& getDisplay() override { return display_; }
    hal::IRtc& getRtc() override { return rtc_; }
    hal::IBattery& getBattery() override { return battery_; }
    hal::IInput& getInput() override { return input_; }
    hal::IBluetooth& getBluetooth() override { return bluetooth_; }
    hal::ICompanionSource& getCompanionSource() override { return bluetooth_; }
    hal::IWifiRadio& getWifi() override { return wifi_; }

    uint32_t getUptimeMs() const override;
    void delayMs(uint32_t ms) override;
    void restart() override;

    static BoardTerra& instance();

private:
    BoardConfig config_;
    TerraPins pins_;
    drivers::display::GxEpd2Display display_;
    hal::Esp32Rtc rtc_;
    hal::Esp32Battery battery_;
    hal::Esp32Input input_;
    hal::Esp32Bluetooth bluetooth_;
    hal::Esp32Wifi wifi_;
};

} // namespace board
} // namespace ersa

#endif // ARDUINO
