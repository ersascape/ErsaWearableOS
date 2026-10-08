#if defined(ARDUINO)

#include "hal/esp32/esp32_battery.h"
#include <Arduino.h>
#include <stddef.h>

namespace {

uint8_t voltageToPercent(uint16_t millivolts) {
    // Approximate resting-voltage curve for a single Li-ion/LiPo cell.
    static constexpr uint16_t mvPoints[] = {3300, 3500, 3600, 3700, 3750,
                                             3800, 3850, 3900, 4000, 4100, 4200};
    static constexpr uint8_t pctPoints[] = {0, 2, 5, 12, 25, 42, 58, 72, 88, 96, 100};
    constexpr size_t count = sizeof(mvPoints) / sizeof(mvPoints[0]);
    if (millivolts <= mvPoints[0]) return pctPoints[0];
    if (millivolts >= mvPoints[count - 1]) return pctPoints[count - 1];
    for (size_t i = 1; i < count; ++i) {
        if (millivolts <= mvPoints[i]) {
            const uint16_t spanMv = mvPoints[i] - mvPoints[i - 1];
            const uint8_t spanPct = pctPoints[i] - pctPoints[i - 1];
            return pctPoints[i - 1] + ((millivolts - mvPoints[i - 1]) * spanPct) / spanMv;
        }
    }
    return 0;
}

} // namespace

namespace ersa {
namespace hal {

Esp32Battery::Esp32Battery(const board::BatteryPins& pins)
    : pins_(pins) {}

Result<void> Esp32Battery::init() {
    pinMode(pins_.adc.number, INPUT);
    analogSetPinAttenuation(pins_.adc.number, ADC_11db);
    sample();
    return Result<void>();
}

void Esp32Battery::sample() {
    uint32_t sumMv = 0;
    constexpr uint8_t samples = 24;
    for (uint8_t i = 0; i < samples; ++i) {
        sumMv += analogReadMilliVolts(pins_.adc.number);
        delayMicroseconds(500);
    }
    // The BSP supplies divider/gauge scaling; the driver only applies it.
    const uint32_t scaleDenominator = pins_.voltageScaleDenominator ? pins_.voltageScaleDenominator : 1;
    const uint32_t battMv = (sumMv / samples) * pins_.voltageScaleNumerator / scaleDenominator;
    if (battMv >= 2500 && battMv <= 4500) {
        connected_ = true;
        cachedMillivolts_ = hasVoltageSample_ ? (cachedMillivolts_ * 3U + battMv) / 4U : battMv;
        hasVoltageSample_ = true;
        cachedPercentage_ = voltageToPercent(cachedMillivolts_);
    } else {
        connected_ = false;
        cachedMillivolts_ = static_cast<uint16_t>(battMv);
        cachedPercentage_ = 0;
        hasVoltageSample_ = false;
    }
}

uint16_t Esp32Battery::millivolts() const {
    return cachedMillivolts_;
}

uint8_t Esp32Battery::percentage() const {
    return cachedPercentage_;
}

bool Esp32Battery::isConnected() const {
    return connected_;
}

bool Esp32Battery::isCharging() const {
    // Terra has no charger-status input; voltage alone cannot distinguish
    // charging from a freshly charged or unloaded cell.
    return false;
}

} // namespace hal
} // namespace ersa

#endif // ARDUINO
