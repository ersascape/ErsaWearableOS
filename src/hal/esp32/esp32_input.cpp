#if defined(ARDUINO)

#include "hal/esp32/esp32_input.h"
#include "core/buttons.h"
#include "core/debug_log.h"
#include <Arduino.h>

namespace ersa {
namespace hal {

Esp32Input::Esp32Input(const board::ButtonPins& pins, events::EventBus& bus)
    : pins_(pins), bus_(bus) {}

Result<void> Esp32Input::init() {
    Buttons::begin(pins_.top.number, pins_.bottom.number,
                   pins_.top.pull == board::PinPull::Up ? Buttons::Pull::Up :
                       pins_.top.pull == board::PinPull::Down ? Buttons::Pull::Down : Buttons::Pull::None,
                   pins_.bottom.pull == board::PinPull::Up ? Buttons::Pull::Up :
                       pins_.bottom.pull == board::PinPull::Down ? Buttons::Pull::Down : Buttons::Pull::None,
                   pins_.top.activeLevel == board::PinActiveLevel::Low,
                   pins_.bottom.activeLevel == board::PinActiveLevel::Low);
    return Result<void>();
}

void Esp32Input::poll() {
    Buttons::tick();
    const auto legacy = Buttons::takeEvent();
    if (legacy == Buttons::Event::None) return;
    // A simultaneous hold is reserved for the forced-restart gesture.
    if (isPressed(events::ButtonId::Button1) && isPressed(events::ButtonId::Button2)) return;

    events::EventType type = events::EventType::None;
    events::ButtonId btn = events::ButtonId::Unknown;

    switch (legacy) {
        case Buttons::Event::Next:
            btn = events::ButtonId::Button1;
            type = events::EventType::ButtonClicked;
            break;
        case Buttons::Event::Previous:
            btn = events::ButtonId::Button1;
            type = events::EventType::ButtonDoubleClicked;
            break;
        case Buttons::Event::Home:
            btn = events::ButtonId::Button1;
            type = events::EventType::ButtonLongPressed;
            break;
        case Buttons::Event::Action:
            btn = events::ButtonId::Button2;
            type = events::EventType::ButtonClicked;
            break;
        case Buttons::Event::ActionAlt:
            btn = events::ButtonId::Button2;
            type = events::EventType::ButtonDoubleClicked;
            break;
        case Buttons::Event::ActionLong:
            btn = events::ButtonId::Button2;
            type = events::EventType::ButtonLongPressed;
            break;
        default:
            return;
    }

    events::Event evt = events::Event::createButton(type, btn, millis());
    DebugLog::log("INPUT: routing button=%u event=%u", unsigned(btn), unsigned(type));
    bus_.post(evt);
}

bool Esp32Input::isPressed(events::ButtonId button) const {
    if (button == events::ButtonId::Button1) {
        return digitalRead(pins_.top.number) ==
            (pins_.top.activeLevel == board::PinActiveLevel::Low ? LOW : HIGH);
    } else if (button == events::ButtonId::Button2) {
        return digitalRead(pins_.bottom.number) ==
            (pins_.bottom.activeLevel == board::PinActiveLevel::Low ? LOW : HIGH);
    }
    return false;
}

bool Esp32Input::hasPendingEvents() const {
    return Buttons::hasPendingEvents();
}

} // namespace hal
} // namespace ersa

#endif // ARDUINO
