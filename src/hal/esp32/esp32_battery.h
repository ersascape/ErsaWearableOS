#pragma once

#if defined(ARDUINO)

#include "ersa/hal/battery.h"

namespace ersa {
namespace hal {

/** ESP32-C3 battery adapter that scales and filters a board ADC input. */
class Esp32Battery : public IBattery {
public:
    /** Bind the ADC channel supplied by the selected BSP pin map. */
    explicit Esp32Battery(int adcPin = 2);
    ~Esp32Battery() override = default;

    Result<void> init() override;
    void sample() override;
    uint16_t millivolts() const override;
    uint8_t percentage() const override;
    bool isConnected() const override;
    bool isCharging() const override;

private:
    int adcPin_;
};

} // namespace hal
} // namespace ersa

#endif // ARDUINO
