#pragma once

namespace ersa {
namespace board {

enum class PinFunction { Gpio, I2c, Spi, Adc };
enum class PinPull { None, Up, Down };
enum class PinActiveLevel { High, Low };
enum class PinDirection { Input, Output, Peripheral };

struct Pin {
    int number{-1};
    PinFunction function{PinFunction::Gpio};
    PinPull pull{PinPull::None};
    PinActiveLevel activeLevel{PinActiveLevel::High};
    PinDirection direction{PinDirection::Peripheral};
};

struct I2cPins { Pin sda; Pin scl; };
struct SpiPins { Pin clock; Pin controllerOut; Pin controllerIn; };
struct DisplayPins { Pin chipSelect; Pin dataCommand; Pin reset; Pin busy; };
struct ButtonPins { Pin top; Pin bottom; };
struct BatteryPins { Pin adc; };

// Board pinctrl contract: groups related signals by peripheral and describes
// the electrical behavior needed when the platform configures each pin.
class Pins {
public:
    virtual ~Pins() = default;
    virtual const I2cPins& i2c() const = 0;
    virtual const SpiPins& spi() const = 0;
    virtual const DisplayPins& display() const = 0;
    virtual const ButtonPins& buttons() const = 0;
    virtual const BatteryPins& battery() const = 0;
};

} // namespace board
} // namespace ersa
