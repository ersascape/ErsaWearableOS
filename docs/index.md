# EWP developer wiki

This wiki is the entry point for working on Ersa Wearable Platform firmware,
board support, hardware, and host tools. It documents the code in this
repository and labels planned integrations as planned.

## Start here

- [Project overview and build commands](https://github.com/ersascape/ErsaWearableOS#readme)
- [Latest release notes](https://github.com/ersascape/ErsaWearableOS/blob/master/release-notes/ewp-0.3.1.md)
- [C++ API reference](https://pkgs-wearables.ersa.dev/wiki/api/): generated classes, methods, headers, and source documentation.
- [Wearable project](https://open.ersa.dev/wearable): product and project landing page.
- [Wearables package repository](https://pkgs-wearables.ersa.dev/): downloadable `ewctl` packages and releases.
- [Architecture and hardware boundaries](HAL_ARCHITECTURE.md)
- [Platform layers and runtime scheduler](PLATFORM_RUNTIME_ARCHITECTURE.md)
- [Build and register apps](BUILDING_APPS.md)
- [Add or port a board](ADDING_A_BOARD.md)
- [Feature catalog](FEATURES.md)

## Firmware and hardware

- [Power and battery design](POWER_AND_BATTERY_DESIGN.md)
- [Release history](https://github.com/ersascape/ErsaWearableOS/blob/master/CHANGELOG.md)
- [Current architecture and power audit](ARCHITECTURE_AND_POWER_AUDIT.md)
- [PCB pin map](pcb-pin-map.md)
- [Apple Bluetooth connectivity](apple-connectivity.md)
- [USB developer control and `ewctl`](ewctl-control-bridge.md)

## API reference

The [published C++ API reference](https://pkgs-wearables.ersa.dev/wiki/api/)
covers project headers, source, services, board support, platform HALs,
peripheral drivers, and tests. Class and function pages explain their purpose,
behavior, constraints, side effects, and design rationale. Build it locally
with `make docs`; the result is `api/index.html` in `.pio/wiki-site/`.
Third-party Arduino and ESP-IDF SDK sources are maintained upstream and are
linked from the app-building guide rather than copied into this reference.

## Contributing changes

Run `make test`, `make firmware`, and `make docs` for changes that affect the
firmware or its interfaces. Keep board wiring in the BSP, vendor APIs in the
matching platform HAL or peripheral driver, and update the relevant wiki page
when behavior changes.
