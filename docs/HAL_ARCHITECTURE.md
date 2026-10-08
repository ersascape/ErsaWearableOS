# HAL and Board Architecture

The hardware boundary follows the same separation used by Android's vendor HAL
model: framework code calls stable domain contracts, board composition selects
implementations, and platform/peripheral drivers own vendor APIs. AOSP's exact
Binder/AIDL service machinery is not a fit for this single-process ESP-IDF
firmware, but the dependency direction is useful here.

```text
apps and services
        │ domain contracts
        ▼
include/ersa/hal/       stable interfaces and platform-neutral types
        ▲
        │ implemented by
src/hal/<platform>/     MCU/RTOS integration
src/drivers/<domain>/   peripheral controller/device implementations
        ▲
        │ composed by
src/bsp/<manufacturer>/<platform>/<codename>/
```

The selected BSP provides `Board::current()` through its
`boardImplementation()` provider before peripherals initialize. The BSP owns
pin maps and constructs implementations. It returns interfaces
such as `IDisplay`, `IBluetooth`, `IRtc`, `IBattery`, `IInput`, and
`IWifiRadio`; apps and services should not cast those references to concrete
drivers. Peripheral-specific behavior belongs under `src/drivers`, while
platform adapters belong under `src/hal/<platform>`.

Power policy uses `IPowerManagement`. It owns CPU frequency setup and DVFS
locks, idle-sleep gating, task wake waits, GPIO and timer wake sources, and
light/deep sleep entry. UI code provides the board pin map and asks the HAL to
arm wake sources; it does not call ESP-IDF power, sleep, GPIO interrupt, or
FreeRTOS notification APIs directly. Bluetooth has the separate `IBluetooth`
and `ICompanionSource` contracts, implemented by the ESP32 Bluetooth adapter.

`IDisplay` contains the graphics operations used by the UI, with logical font
and color values. The Terra BSP selects a custom SSD1681 driver adapted from
T1E V2a for the GDEY0154D67 panel. It owns the panel protocol, waveform tables,
SPI transport, and canvas. Renderers use only `IDisplay`; they do not include
Adafruit GFX or access a concrete display object.

`IRtc` carries calendar values and timekeeping diagnostics. The Terra BSP binds
`Esp32Rtc`, which composes the custom DS3231 register driver. Date conversion
and display-facing calendar values use the first party `CalendarTime` type;
RTClib is not part of the build.

The board pin contract groups I2C, SPI, display, buttons, and ADC pins. Pin
records include function, direction, pull, and active level. The platform pin
controller applies GPIO electrical settings; the peripheral drivers route bus
signals through the platform bus APIs. This keeps pin ownership in the BSP
without introducing Linux Device Tree into an ESP-IDF build that does not
consume DTS bindings.

ESP-IDF Kconfig selects the board product and its required drivers. The BSP
provider is guarded by the selected board symbol. Keep
`sdkconfig.defaults` as the reproducible default configuration and use
`menuconfig` for local tuning. The Terra BSP selects its required SSD1681 panel
driver, and the component build compiles only that panel's sources.

There is no audio playback or audio peripheral in this firmware. BLE media
controls and metadata are companion-source features, not an audio HAL. Add an
`IAudio` contract when the product gains audio input/output hardware and a
real implementation to bind.
