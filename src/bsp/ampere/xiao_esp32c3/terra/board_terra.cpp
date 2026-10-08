#if defined(ARDUINO)

#include "bsp/ampere/xiao_esp32c3/terra/board_terra.h"
#include "hal/esp32/esp32_pin_controller.h"
#include <Arduino.h>

namespace ersa {
namespace board {

const DeviceInfo& terraDeviceInfo();

BoardTerra& BoardTerra::instance() {
    static BoardTerra s_board;
    return s_board;
}

BoardTerra::BoardTerra()
    : display_(pins_.display().chipSelect.number, pins_.display().dataCommand.number,
               pins_.display().reset.number, pins_.display().busy.number,
               pins_.spi().clock.number, pins_.spi().controllerIn.number, pins_.spi().controllerOut.number),
      rtc_(pins_.i2c().sda.number, pins_.i2c().scl.number),
      battery_(pins_.battery().adc.number),
      input_(pins_.buttons().top.number, pins_.buttons().bottom.number) {
    config_.name = getDeviceInfo().name;
    config_.capabilities.wifi = true;
    config_.capabilities.bluetooth = true;
    config_.capabilities.rtc = true;
    config_.capabilities.batteryGauge = true;
    config_.capabilities.buttons = true;
    config_.capabilities.haptics = false;
    config_.capabilities.touch = false;

    config_.display.width = 200;
    config_.display.height = 200;
    config_.display.partialRefresh = true;
    config_.display.isEpaper = true;
}

const DeviceInfo& BoardTerra::getDeviceInfo() const {
    return terraDeviceInfo();
}

Result<void> BoardTerra::init() {
    Board::setCurrent(this);
    hal::Esp32PinController::apply(pins_);
    display_.setBusyCallback([](void* context) {
        static_cast<hal::IInput*>(context)->poll();
    }, &input_);
    const auto wifiResult = wifi_.init();
    if (wifiResult.isError()) return wifiResult;
    // The board owns construction and pin mapping. Service managers initialize
    // RTC, display, battery and BLE in the system boot sequence; initializing
    // those devices here too caused duplicate peripheral/radio startup.
    return input_.init();
}

uint32_t BoardTerra::getUptimeMs() const {
    return millis();
}

void BoardTerra::delayMs(uint32_t ms) {
    delay(ms);
}

void BoardTerra::restart() {
    ESP.restart();
}

} // namespace board
} // namespace ersa

#endif // ARDUINO
