#if defined(ARDUINO)

#include "hal/esp32/esp32_pin_controller.h"
#include <Arduino.h>

namespace ersa {
namespace hal {
namespace {

void configure(const board::Pin& pin) {
    if (pin.number < 0 || pin.direction == board::PinDirection::Peripheral) return;

    if (pin.direction == board::PinDirection::Output) {
        const bool inactiveLevel = pin.activeLevel == board::PinActiveLevel::Low;
        digitalWrite(pin.number, inactiveLevel ? HIGH : LOW);
        pinMode(pin.number, OUTPUT);
        return;
    }

    if (pin.pull == board::PinPull::Up) pinMode(pin.number, INPUT_PULLUP);
    else if (pin.pull == board::PinPull::Down) pinMode(pin.number, INPUT_PULLDOWN);
    else pinMode(pin.number, INPUT);
}

} // namespace

void Esp32PinController::apply(const board::Pins& pins) {
    const auto& display = pins.display();
    const auto& buttons = pins.buttons();
    configure(display.chipSelect);
    configure(display.dataCommand);
    configure(display.reset);
    configure(display.busy);
    configure(buttons.top);
    configure(buttons.bottom);
    configure(pins.battery().adc);
}

} // namespace hal
} // namespace ersa

#endif // ARDUINO
