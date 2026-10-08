# Platform and runtime architecture

This page describes the current platform boundary and runtime scheduler. The
firmware is still being migrated in small steps so each move can be built and
tested independently.

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
  `events::Event` handling. Single and double clicks use a 250 ms recognition
  window; holding the top button emits HOME, which returns an app launched from
  the drawer to the drawer.
- **USB console transport:** UI sleep policy, debug logs, and the USB control
  bridge now use `IConsole`; the USB Serial/JTAG implementation is
  `Esp32UsbConsole`. Reset, heap, NVS, and OTA metadata access in the control
  and diagnostics code still needs a platform diagnostics/OTA boundary.
- **Local web transport:** the captive portal now uses generic HTTP request/
  response and DNS responder contracts. The ESP32 adapter owns Arduino
  `WebServer`, `DNSServer`, and `IPAddress`; the app owns its HTML and settings
  behavior without including those framework types.
- **Outbound network transport:** CalDAV and time sync now depend on generic
  streaming HTTP and NTP client contracts. The ESP32 adapters own
  `HTTPClient`, `WiFiClientSecure`, and ESP-IDF SNTP. `NetSync` retains product
  policy, response parsing, timeout decisions, and task orchestration; its
  remaining FreeRTOS task and heap diagnostics calls are platform migration
  work.
- **RTC:** `Esp32Rtc` adapts the generic `IRtc` contract to the custom DS3231
  register driver. It owns bus setup, oscillator-stop handling, verification,
  and bounded software-time interpolation/reconciliation. `WatchClock` is a
  compatibility facade returning the first party `CalendarTime` value; RTClib
  has been removed from the dependency graph.
- **Still to migrate:** network sync still uses FreeRTOS task controls and
  ESP-IDF heap diagnostics in `src/core/net_sync.cpp`; USB control still uses ESP-IDF OTA,
  partition, reset, heap, and CPU-frequency APIs; OTA transfer implementation
  still uses Arduino/ESP-IDF in the service. Move these behind network/time-sync,
  portal, OTA, and platform-diagnostics contracts respectively.
- **Storage:** `WatchConfig`, calendar cache, and boot history now use the generic typed/byte storage contract, and
  the `Preferences` implementation is installed by the ESP32 HAL. Product
  configuration and app data share the `ersa_nvs` namespace with short,
  prefixed keys. This is a deliberate schema reset; deployed users must factory
  reset or provision settings again after this change.

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
5. Block until an event or the earliest registered deadline.

`runtime::SleepEligibility` combines console, interaction, active-app,
network, display, lease, and pending-work constraints. `RuntimeScheduler`
turns that policy into one sleep/wait plan, while `WakeDeadlineSet` selects the
earliest delay without dynamic allocation. Once eligible, the task waits for
the actual next deadline; it does not wake every 10 ms to poll buttons. GPIO
interrupts notify the task on button transitions, and BLE/network callbacks
notify it when queued work arrives. A held button keeps the loop awake so
debounce, click, and hold behavior continues to progress.

Automatic light sleep becomes eligible after 5 seconds without deliberate
button activity. USB console attachment, foreground portal/call work, network
sync, display BUSY, wake-lock leases, or pending UI/input work keep the
no-light-sleep guard held. Passive BLE traffic wakes the task but does not
restart the user-idle timer. After each wake the software clock is reconciled
with the DS3231 before the next minute deadline is calculated. The display
driver cuts e-paper drive voltage after a refresh; the visible image remains
without a separate display-manager idle transition.

The UI still collects service deadlines and owns screen timeout policy. The
next runtime step is to have each service expose its own next deadline and move
app timeout policy out of `WatchUi::tick()`.

Keep the scheduler fixed-capacity and allocation-free. Add one deadline at a
time with a host test proving that it wakes early enough; notifications remain
the fast path for asynchronous BLE, input, and network completion. The ESP32
power HAL uses FreeRTOS tickless idle for automatic BLE-coordinated light
sleep instead of directly calling a chip sleep function from the UI loop.

## Validation

Run `make test`, `make firmware`, and `make docs`. Host tests can verify
deadline ordering, sleep blockers, event ownership, and service policy. They
cannot prove GPIO electrical behavior, BLE reconnection, light-sleep residency,
or battery current; those still require on-device checks.
