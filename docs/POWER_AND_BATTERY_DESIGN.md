# Battery and power management design

## What this board can control today

The supplied EasyEDA PCB JSON confirms the main LiPo connects to the XIAO
module's battery pads. R3 and R4 are each 220 kOhm: R3 connects battery positive
to their midpoint, R4 connects the midpoint to battery return, and that midpoint
routes to XIAO pad 1 / GPIO2 / A0. The existing `ADC millivolts * 2` conversion
matches this 1:1 divider. The separately mounted CR927 holder is for RTC backup,
not the watch's main battery. The component list contains no separate fuel gauge
or charger IC; charging and protection are on the XIAO module.

The board does not expose charger enable, charger status, battery current, or
cell temperature to firmware. The XIAO's onboard charger/protection therefore
operates autonomously. Firmware can estimate cell voltage, notify the user,
reduce system load, and choose a safe sleep state; it cannot implement charge
termination or replace the cell's independent protection circuit.

The current ADC implementation reports a voltage-only state-of-charge estimate.
The divider ratio is confirmed, but the ADC reading should still be compared
with a meter on the assembled watch. Voltage shifts with load and temperature,
so a voltage curve cannot report precise remaining capacity or predict runtime.
The code averages ADC readings, filters sample-to-sample variation, and uses an
explicit Li-ion voltage lookup table; those changes improve stability, not
absolute accuracy. Charging state is unknown with the present pin map and must
not be inferred from cell voltage alone.

The firmware policy now requires three consecutive critical samples (10 seconds
apart) before entering a 10-minute deep-sleep charge-check cycle; buttons also
wake it. Low and critical voltage thresholds use hysteresis. Validate these
thresholds with the actual cell under load before relying on them: they are
conservative starting points, not characterized limits for this assembled watch.

## BLE-preserving light sleep

The PlatformIO `framework = arduino, espidf` build enables
`CONFIG_PM_ENABLE`, `CONFIG_FREERTOS_USE_TICKLESS_IDLE`, and Bluetooth controller
modem sleep in `sdkconfig.defaults`. The generated
`.pio/build/ErsaWearable/config/sdkconfig.h` confirms these options are active
for the firmware build. `Esp32PowerManagement::initialize()` calls
`esp_pm_configure()` with `light_sleep_enable = true`; the UI releases its
`ESP_PM_NO_LIGHT_SLEEP` lock after 5 seconds without button activity when USB,
display, network sync, foreground work, pending input, and feature-lease
constraints permit sleep. USB console attachment intentionally blocks light
sleep so the control connection remains available. FreeRTOS tickless idle then
coordinates automatic light sleep with task deadlines and power locks. GPIO
interrupts wake the UI task, so idle waits can use the next real service
deadline instead of polling buttons every 10 ms.

The Bluetooth controller uses modem sleep mode 1 and the main crystal as its
low-power clock. `CONFIG_BT_CTRL_MAIN_XTAL_PU_DURING_LIGHT_SLEEP` is enabled, so
the controller can use the main crystal during automatic light sleep without
holding its `ESP_PM_NO_LIGHT_SLEEP` lock. The separate DS3231 clock is not
connected to the ESP32-C3 low-power clock pins and is not used for this purpose.
The code does not call `esp_light_sleep_start()` from the UI loop; manual sleep
would bypass the controller's coordination.

The e-paper driver switches off panel drive voltage after each refresh while
retaining the visible image. The display manager handles refresh timing and
forces a full waveform after 360 partial frames. A display-refresh performance
scope holds the platform frequency lock only around rendering and the physical
waveform, then releases it for automatic frequency management.

These build settings and code paths enable BLE-compatible automatic light sleep,
but they do not prove that a particular device spends time in light sleep or
that its BLE link remains stable under all negotiated connection parameters.
Validate sleep residency, BLE stability, button wake latency, e-paper refresh,
Wi-Fi sync, and current draw on hardware. The main-crystal clock choice favors
BLE timing and link compatibility over the lowest possible sleep current.

ESP-IDF references: [Power Management](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32c3/api-reference/system/power_management.html), [Bluetooth low-power clock Kconfig](https://github.com/espressif/esp-idf/blob/v4.4/components/bt/controller/esp32c3/Kconfig.in).

## HAL boundaries

Keep policy independent from ESP32, charger, and gauge drivers:

```text
Application / UI
      | battery and power events
BatteryPowerService (policy, thresholds, hysteresis, persistence)
      |                 |
IBatteryGauge       IPowerControl
      |                 |
ESP32 ADC /          ESP32 CPU, BLE, Wi-Fi,
fuel-gauge driver     display and sleep driver

IChargeController (optional; only where hardware exposes safe controls/status)
      |
charger IC driver
```

`IBatteryGauge` should return a snapshot rather than isolated guesses:

```cpp
struct BatterySnapshot {
    uint16_t millivolts;
    int16_t milliamps;       // optional; absent when the board has no current sensor
    int16_t temperatureC10;  // optional; absent when the board has no sensor
    uint8_t stateOfCharge;   // 0..100, plus a validity/source flag
    bool present;
    bool charging;
    bool chargerStateKnown;
    bool fault;
};
```

The service owns filtering and policy, and emits events for normal, low, and
critical battery states. Thresholds need hysteresis and time qualification so
radio bursts or e-paper refresh dips do not flap the UI. Critical policy should
stop optional Wi-Fi sync, reduce BLE advertising, postpone nonessential display
refreshes, save settings, warn once, then enter a board-supported low-power
state before the regulator reaches brownout. Recovery requires voltage above a
higher threshold for a dwell period. The exact thresholds must be measured on
the assembled board under radio and display load.

`IPowerControl` owns board-specific actions such as radio mode, CPU frequency,
display power, and sleep/wake. The policy service requests an operating profile;
it does not call ESP-IDF sleep APIs directly. Interrupt-driven buttons can wake
the board, but sleep must not be enabled until BLE reconnect, RTC wake, and
e-paper behavior are validated together.

`IChargeController` is optional and separate from the gauge. It may expose
charger status or safe input-current/enable controls only when the selected IC
and board wiring support them. Cell overvoltage, undervoltage, overcurrent, and
temperature protection must remain in hardware and function with firmware
stopped.

## Hardware needed for full management

For useful SOC and robust early brownout handling, use a fuel gauge connected to
the cell (for example a gauge with a documented I2C interface) and expose its
alert output to a wake-capable GPIO. To manage charge policy, select a charger
with documented status and controllable inputs, route those signals to the MCU,
and include a battery temperature sensor. Keep a separate cell-protection path
that disconnects the cell on electrical faults even if the MCU or firmware has
failed. Confirm regulator dropout and peak load with oscilloscope measurements.

Before choosing components or thresholds, record the cell part/capacity, actual
divider resistor values, charger IC, protection IC, regulator, and loaded voltage
at brownout. Without these, firmware can provide conservative policy, but cannot
promise an accurate percentage, remaining runtime, or controlled charging.

## Bring-up and validation

1. Compare logged ADC-derived millivolts with a multimeter at full charge,
   mid-discharge, and near the board's safe shutdown point; tune divider scale.
2. Log voltage at a fixed cadence with BLE idle, BLE connected, during e-paper
   refresh, and during Wi-Fi sync. Measure cell voltage at the same time to
   quantify transient sag and ADC error.
3. Verify low/critical thresholds with a bench supply or protected test cell;
   confirm orderly shutdown happens before ESP32 brownout and that wake/recovery
   has hysteresis.
4. Test charging only through the charger IC's documented behavior and confirm
   its protections independently of firmware.
