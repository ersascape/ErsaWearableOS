#pragma once

#include <stdint.h>

namespace ersa {
namespace board {

/** Electrical/peripheral function selected for a logical board pin. */
enum class PinFunction { Gpio, I2c, Spi, Adc };
/** Internal pull resistor state configured by platform pinctrl. */
enum class PinPull { None, Up, Down };
/** Logic level considered asserted by the peripheral or application. */
enum class PinActiveLevel { High, Low };
/** Direction used when configuring the pad. Peripheral delegates direction to the bus block. */
enum class PinDirection { Input, Output, Peripheral };

/** Complete logical and electrical setup for one MCU pad. */
struct Pin {
    /** MCU GPIO number; -1 means the board does not populate this signal. */
    int number{-1};
    /** Peripheral mux function requested for this pad. */
    PinFunction function{PinFunction::Gpio};
    /** Pull resistor setting used to establish a defined idle state. */
    PinPull pull{PinPull::None};
    /** Asserted logic level; records active-low buttons and controls explicitly. */
    PinActiveLevel activeLevel{PinActiveLevel::High};
    /** Input/output ownership requested from the platform pin controller. */
    PinDirection direction{PinDirection::Peripheral};
};

/** Pin pair routed to the board's I2C controller. */
struct I2cPins { Pin sda; Pin scl; };
/** Signals routed to the board's SPI controller. */
struct SpiPins { Pin clock; Pin controllerOut; Pin controllerIn; };
/** Dedicated control pins for a display attached to a shared serial bus. */
struct DisplayPins { Pin chipSelect; Pin dataCommand; Pin reset; Pin busy; };
/** User-visible button pins, in top and bottom product positions. */
struct ButtonPins { Pin top; Pin bottom; };
/** Analog input and board scaling used to interpret a battery sense signal. */
struct BatteryPins {
    /** ADC input routed to the divider or fuel-gauge output. */
    Pin adc;
    /** Multiplier applied to measured ADC voltage to recover battery voltage. */
    uint8_t voltageScaleNumerator{1};
    /** Divisor paired with voltageScaleNumerator; must be nonzero. */
    uint8_t voltageScaleDenominator{1};
};

/**
 * Board-specific pinctrl description consumed by platform setup code.
 *
 * Implement this once per BSP and return stable references to immutable signal
 * groups. Grouping by peripheral gives bus/HAL initialization a typed contract;
 * describing pull, direction, and active level here keeps electrical facts out
 * of application logic and prevents a platform driver from guessing board pins.
 */
class Pins {
public:
    /** Permit destruction through the common pin-map contract. */
    virtual ~Pins() = default;
    /** Return the I2C clock/data assignment used by board peripherals. */
    virtual const I2cPins& i2c() const = 0;
    /** Return the shared SPI signal assignment used by display or storage. */
    virtual const SpiPins& spi() const = 0;
    /** Return the display's chip select and control signal assignments. */
    virtual const DisplayPins& display() const = 0;
    /** Return the logical top/bottom button assignments and polarity. */
    virtual const ButtonPins& buttons() const = 0;
    /** Return the analog battery-sense assignment. */
    virtual const BatteryPins& battery() const = 0;
};

} // namespace board
} // namespace ersa
