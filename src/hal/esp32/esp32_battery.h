#pragma once

#if defined(ARDUINO)

#include "ersa/hal/battery.h"
#include "ersa/board/pins.h"

namespace ersa {
namespace hal {

/** ESP32-C3 battery adapter that scales and filters a board ADC input. */
class Esp32Battery : public IBattery {
public:
    /** Bind the ADC channel supplied by the selected BSP pin map. */
    explicit Esp32Battery(const board::BatteryPins& pins);
    ~Esp32Battery() override = default;

    Result<void> init() override;
    void sample() override;
    uint16_t millivolts() const override;
    uint8_t percentage() const override;
    bool isConnected() const override;
    bool isCharging() const override;

private:
    board::BatteryPins pins_;
    uint16_t cachedMillivolts_{0};
    uint8_t cachedPercentage_{0};
    bool connected_{false};
    bool hasVoltageSample_{false};
};

} // namespace hal
} // namespace ersa

#endif // ARDUINO
