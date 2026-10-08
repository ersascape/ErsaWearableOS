#include "battery.h"
#include "ersa/board/board.h"
#include "ui/watch_icons.h"
#include "core/debug_log.h"
#include <Arduino.h>

namespace Battery {

namespace {
uint16_t cachedMv = 0;
uint8_t cachedPercent = 100;
bool connected = false;
bool hasVoltageSample = false;
uint32_t lastSampleTime = 0;

uint8_t mvToPercent(uint16_t mv) {
    // Approximate resting-voltage curve for a single Li-ion/LiPo cell.
    // It is intentionally a LUT so board/cell calibration can be substituted.
    static constexpr uint16_t mvPoints[] = {3300, 3500, 3600, 3700, 3750,
                                              3800, 3850, 3900, 4000, 4100, 4200};
    static constexpr uint8_t pctPoints[] = {0, 2, 5, 12, 25, 42, 58, 72, 88, 96, 100};
    constexpr size_t count = sizeof(mvPoints) / sizeof(mvPoints[0]);
    if (mv <= mvPoints[0]) return pctPoints[0];
    if (mv >= mvPoints[count - 1]) return pctPoints[count - 1];
    for (size_t i = 1; i < count; ++i) {
        if (mv <= mvPoints[i]) {
            const uint16_t spanMv = mvPoints[i] - mvPoints[i - 1];
            const uint8_t spanPct = pctPoints[i] - pctPoints[i - 1];
            return pctPoints[i - 1] + ((mv - mvPoints[i - 1]) * spanPct) / spanMv;
        }
    }
    return 0;
}

void sample() {
    uint32_t sumMv = 0;
    constexpr uint8_t SAMPLES = 24;
    for (uint8_t i = 0; i < SAMPLES; ++i) {
        sumMv += analogReadMilliVolts(ersa::board::Board::current().getPins().battery().adc.number);
        delayMicroseconds(500);
    }
    const uint32_t rawMv = sumMv / SAMPLES;

    // Assuming standard 1:1 (half-voltage) divider: V_batt = V_adc * 2
    const uint32_t battMv = rawMv * 2;

    if (battMv >= 2500 && battMv <= 4500) {
        connected = true;
        // Smooth load-related sag/noise between periodic samples. First sample
        // initializes directly so boot diagnostics are useful immediately.
        cachedMv = hasVoltageSample ? (cachedMv * 3U + battMv) / 4U : battMv;
        hasVoltageSample = true;
        cachedPercent = mvToPercent(cachedMv);
    } else {
        // Invalid/out-of-range ADC indicates no usable battery measurement.
        connected = false;
        cachedMv = battMv;
        cachedPercent = 0;
        hasVoltageSample = false;
    }
}
} // namespace

void begin() {
    pinMode(ersa::board::Board::current().getPins().battery().adc.number, INPUT);
    analogSetPinAttenuation(ersa::board::Board::current().getPins().battery().adc.number, ADC_11db);
    sample();
    DebugLog::log("BATTERY init: raw_mv=%u connected=%d pct=%u%%",
                  cachedMv, connected, cachedPercent);
}

void tick() {
    if (millis() - lastSampleTime >= 10000) { // Sample every 10 seconds
        lastSampleTime = millis();
        sample();
    }
}

uint16_t millivolts() { return cachedMv; }
uint8_t percentage() { return cachedPercent; }
bool isConnected() { return connected; }

const uint8_t* iconBitmap() {
    if (!connected) return WatchIcons::batteryFull;
    if (cachedMv >= 4250) return WatchIcons::batteryCharging;
    if (cachedPercent >= 85) return WatchIcons::batteryFull;
    if (cachedPercent >= 70) return WatchIcons::battery83;
    if (cachedPercent >= 55) return WatchIcons::battery67;
    if (cachedPercent >= 35) return WatchIcons::battery50;
    if (cachedPercent >= 20) return WatchIcons::battery33;
    return WatchIcons::battery17;
}

} // namespace Battery
