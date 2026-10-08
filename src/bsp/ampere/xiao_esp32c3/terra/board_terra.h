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
#include "hal/esp32/esp32_power_management.h"

namespace ersa {
namespace board {

/**
 * Immutable pinctrl map for the Ampere Terra product.
 * GPIO numbers, bus muxes, pulls, and asserted levels live here because they
 * are facts about this PCB; Esp32PinController interprets the map, while the
 * HAL and app layers consume logical button/bus contracts.
 */
class TerraPins final : public Pins {
public:
    /** Return Terra's RTC and peripheral I2C pins. */
    const I2cPins& i2c() const override { return i2c_; }
    /** Return Terra's shared display SPI pins. */
    const SpiPins& spi() const override { return spi_; }
    /** Return Terra's display-specific GPIO controls and busy input. */
    const DisplayPins& display() const override { return display_; }
    /** Return top/bottom active-low button pins. */
    const ButtonPins& buttons() const override { return buttons_; }
    /** Return Terra's battery-divider ADC pin. */
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

/**
 * Ampere Terra board composition for the XIAO ESP32-C3 platform.
 *
 * This class is the only place that constructs the product's concrete drivers
 * and wires them to generic HALs. It owns those objects for the full firmware
 * lifetime, so service references stay stable; application code sees only the
 * Board contract and does not learn about Terra, ESP32, or GxEPD2 types.
 */
class BoardTerra : public Board {
public:
    /** Construct drivers using the immutable Terra pin map. */
    BoardTerra();
    /** Destruction is virtual through Board; drivers follow board lifetime. */
    ~BoardTerra() override = default;

    /** Apply pinctrl and initialize board-owned buses before services start. */
    Result<void> init() override;
    /** Return product-facing name from the static device metadata. */
    const char* getName() const override { return getDeviceInfo().name; }
    /** Return static Terra manufacturer and codename metadata. */
    const DeviceInfo& getDeviceInfo() const override;
    /** Return immutable panel geometry and feature capabilities. */
    const BoardConfig& getConfig() const override { return config_; }
    /** Return the immutable product pinctrl contract. */
    const Pins& getPins() const override { return pins_; }

    /** Return Terra's GxEPD2-backed implementation through generic display HAL. */
    hal::IDisplay& getDisplay() override { return display_; }
    /** Return the DS3231 implementation through generic RTC HAL. */
    hal::IRtc& getRtc() override { return rtc_; }
    /** Return Terra's voltage-divider battery monitor through battery HAL. */
    hal::IBattery& getBattery() override { return battery_; }
    /** Return the debounced active-low button input adapter. */
    hal::IInput& getInput() override { return input_; }
    /** Return the ESP32 BLE transport. */
    hal::IBluetooth& getBluetooth() override { return bluetooth_; }
    /** Return the companion feature provider implemented by the BLE adapter. */
    hal::ICompanionSource& getCompanionSource() override { return bluetooth_; }
    /** Return the ESP32 station/AP radio adapter. */
    hal::IWifiRadio& getWifi() override { return wifi_; }
    /** Return ESP32 clock, sleep, performance, and wake-source control. */
    hal::IPowerManagement& getPowerManagement() override { return powerManagement_; }

    /** Return monotonic ESP32 uptime in milliseconds. */
    uint32_t getUptimeMs() const override;
    /** Delay the current FreeRTOS task through the Arduino platform. */
    void delayMs(uint32_t ms) override;
    /** Restart through the ESP32 platform reset path. */
    void restart() override;

    /** Return the statically composed Terra board object. */
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
    hal::Esp32PowerManagement powerManagement_;
};

} // namespace board
} // namespace ersa

#endif // ARDUINO
