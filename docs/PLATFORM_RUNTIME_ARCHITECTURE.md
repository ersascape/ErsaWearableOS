# Platform and runtime architecture

This page records the target boundary for platform code and the scheduler work
underway. The firmware is being migrated in small steps so each move can be
built and tested independently.

## Layer ownership

```text
Apps and watchfaces
        |
Portable services and application state
        |
Contracts in include/ersa/hal/
        |
ESP32 adapters in src/hal/esp32/  <---->  BSP composition and pinctrl
        |
ESP-IDF / Arduino component / peripheral libraries
```

- `src/bsp/<manufacturer>/<platform>/<codename>/` owns the board's immutable
  pin map, physical capabilities, and concrete object composition. A HAL may
  consume a pin configuration supplied by the BSP; it must not select a Terra
  GPIO number or infer the board revision itself.
- `include/ersa/hal/` contains platform-neutral contracts. Apps and portable
  services use those contracts rather than ESP-IDF, Arduino, `Wire`, GPIO
  numbers, or a peripheral vendor's types.
- `src/hal/esp32/` owns ESP32 GPIO/ADC, I²C, FreeRTOS, ESP-IDF, Arduino
  compatibility, and lifecycle details. A different platform can implement
  the same contracts without changing an app or policy service.
- `src/ersa/services/` owns portable coordination and product policy such as
  battery qualification, sleep eligibility, time-source priority, and
  application lifecycle. It asks HAL contracts to perform hardware actions.
- `src/apps/` and `src/watchfaces/` own presentation and interaction. They
  receive logical events and generic display operations.

The only intentional Arduino entry point is `src/main.cpp` (`setup()` and
`loop()`), which starts the composed runtime. Compatibility includes in product
code are migration work, not a desired layer dependency.

## Current migration state

- **Battery ADC:** sampling and voltage filtering have moved from
  `src/core/battery.cpp` into `Esp32Battery`. `PowerManager` controls sampling
  cadence through `IBattery`; screens read cached values through the service.
  ADC pin and divider scaling are supplied in BSP `BatteryPins`. Terra has no
  charger-status signal, so voltage is not used to claim charging.
- **Button state:** the UI reads logical button state and queued work through
  `IInput`. The OneButton-backed compatibility event adapter and GPIO reads are
  under `src/hal/esp32/`; the BSP supplies each pin's number, pull, and active
  level at initialization.
  Apps still use a legacy button-action enum while they migrate to normalized
  `events::Event` handling.
- **USB console transport:** UI sleep policy, debug logs, and the USB control
  bridge now use `IConsole`; the USB Serial/JTAG implementation is
  `Esp32UsbConsole`. Reset, heap, NVS, and OTA metadata access in the control
  and diagnostics code still needs a platform diagnostics/OTA boundary.
- **RTC:** `Esp32Rtc` now owns `Wire`, the DS3231 driver, and its bounded
  software-time interpolation/reconciliation. The old `WatchClock` functions
  are compatibility wrappers over the generic `IRtc`; their `DateTime` return
  type still leaks RTClib into callers and can be replaced by `TimePoint` in a
  later API migration.
- **Still to migrate:** network sync still performs
  HTTP/NTP operations in `src/core/net_sync.cpp`; configuration and diagnostic
  persistence still use Arduino `Preferences`; app portal code builds response
  strings with Arduino `String`; USB control uses ESP-IDF OTA partition APIs.
  Move these behind RTC, network/time-sync, key-value storage, HTTP response,
  OTA, and platform-diagnostics contracts respectively.

Do not put product rules in an ESP32 adapter while moving a dependency. Keep
hardware sampling and SDK translation in the adapter, and keep filtering,
fallback order, retry policy, and user-visible behavior in portable services.

## Runtime executor and scheduling

The application task is the single owner of app state and event dispatch. A
radio or GPIO callback posts a bounded event and notifies the task; it does not
call app code from the callback context. The task should have a short, regular
sequence:

1. Drain posted input and companion events.
2. Run services and apps whose deadlines are due.
3. Render once when content is dirty and the panel is ready.
4. Ask one sleep coordinator whether active work or a lease blocks sleep.
5. Wait for an event or the earliest registered deadline.

`runtime::SleepEligibility` now combines console, interaction, active-app,
network, display, lease, and pending-work constraints. `WakeDeadlineSet`
selects the earliest delay without dynamic allocation. The UI currently
contributes the RTC minute, BLE retry, battery sample, boot-time fallback,
transient-screen timeout, and media debounce deadlines. The next step is to
have each service expose its own next deadline and move app timeout policy out
of `WatchUi::tick()`.

Keep the scheduler fixed-capacity and allocation-free. Add one deadline at a
time with a host test proving that it wakes early enough; notifications remain
the fast path for asynchronous BLE, input, and network completion. The
ESP32 power HAL continues to use FreeRTOS tickless idle for automatic
BLE-coordinated light sleep instead of directly calling a chip sleep function
from the UI loop.

## Validation

Run `make test`, `make firmware`, and `make docs`. Host tests can verify
deadline ordering, sleep blockers, event ownership, and service policy. They
cannot prove GPIO electrical behavior, BLE reconnection, light-sleep residency,
or battery current; those still require on-device checks.
