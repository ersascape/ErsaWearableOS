# Adding a board

This guide describes the project’s BSP conventions and the steps required to
bring up another board. The BSP path is
`src/bsp/<manufacturer>/<platform>/<codename>/`:

```text
src/bsp/ampere/xiao_esp32c3/terra/
├── board_terra.h
├── board_terra.cpp
└── device_info.cpp
```

The manufacturer identifies the board vendor, platform identifies its shared
compute module or platform family, and codename identifies the product. This is
the EWP directory convention, not a universal C++ standard.

## Board contract and selection

Implement `ersa::board::Board` and a board-specific `Pins` subclass. The
selected BSP provides `ersa::board::boardImplementation()` and returns its
static board instance as a `Board&`. `Board::current()` is available before
peripheral initialization, so startup and services never need to include a
concrete board class. The selected BSP provider is guarded by its Kconfig board
symbol; exactly one provider must be enabled.

The board implementation owns composition: pin maps, board capabilities, and
instances of the platform and peripheral drivers used by the product. It
returns generic interfaces (`IDisplay`, `IRtc`, `IBattery`, `IInput`,
`IBluetooth`, `ICompanionSource`, `IWifiRadio`, and `IPowerManagement`). Keep
concrete driver names out of application and service APIs.

## Pins and pin control

`include/ersa/board/pins.h` groups pin records by I2C, SPI, display, buttons,
and battery. Each pin carries its number, function, direction, pull, and active
level. Put physical assignments in the BSP’s `Pins` subclass, then let the
platform pin controller apply electrical settings and configure buses.

This typed C++ pin contract is the project’s pinctrl description. Device Tree
is useful when the selected RTOS/build system consumes DTS bindings; this
Arduino/ESP-IDF configuration does not, so an unconsumed DTS file would not
configure hardware. ESP-IDF Kconfig selects the board and required driver
options.

## HAL and driver placement

- Add a platform-neutral contract under `include/ersa/hal/` when code needs a
  hardware capability. Examples are `IDisplay`, `IBluetooth`, `IWifiRadio`,
  and `IPowerManagement`.
- Put MCU, RTOS, GPIO controller, clock, and vendor SDK adapters under
  `src/hal/<platform>/`.
- Put chip or peripheral implementations under `src/drivers/<domain>/`.
- Compose and inject those objects from the selected BSP.

The display contract must not expose GxEPD2 or Adafruit GFX types. The same
principle applies to Bluetooth stacks, bus libraries, storage backends, and
power APIs. Create an audio contract when the product has real audio hardware
and an implementation to bind; do not add an unused placeholder interface.

## Bring-up checklist

1. Add `board_<codename>.h/.cpp`, `device_info.cpp`, and a `Pins` subclass in
   the manufacturer/platform/codename directory.
2. Add the board Kconfig symbol in `src/Kconfig`; guard the BSP’s
   `boardImplementation()` definition with that symbol and keep exactly one
   provider selected.
3. Implement every required `Board` method and return generic HAL interfaces.
   Initialize board pin control before polling buttons or starting peripheral
   services.
4. Add or reuse platform and peripheral drivers under their respective
   directories. Keep pin numbers and board identity out of generic HAL
   contracts.
5. Add mock coverage for new contracts and update the feature, architecture,
   and board documentation.
6. Run `make test`, `make firmware`, and `make docs`. Verify button polarity,
   display refresh, radios, sleep/wake sources, and current draw on hardware.

For architecture boundaries, see [HAL and board architecture](HAL_ARCHITECTURE.md).
