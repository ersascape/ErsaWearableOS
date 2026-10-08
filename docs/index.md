# EWP developer wiki

This wiki is the entry point for working on Ersa Wearable Platform firmware,
board support, hardware, and host tools. It documents the code in this
repository and labels planned integrations as planned.

## Start here

- [Project overview and build commands](https://github.com/ersascape/ErsaWearableOS#readme)
- [Architecture and hardware boundaries](HAL_ARCHITECTURE.md)
- [Add or port a board](ADDING_A_BOARD.md)
- [Feature catalog](FEATURES.md)

## Firmware and hardware

- [Power and battery design](POWER_AND_BATTERY_DESIGN.md)
- [Firmware power audit](FIRMWARE_POWER_AUDIT.md)
- [PCB pin map](pcb-pin-map.md)
- [Apple Bluetooth connectivity](apple-connectivity.md)
- [USB developer control and `ewctl`](ewctl-control-bridge.md)

## API reference

The generated C++ API reference covers public contracts, services, board
support, platform HALs, drivers, and tests. Build it with `make docs`; the
result is `api/index.html` in the generated site.

## Contributing changes

Run `make test`, `make firmware`, and `make docs` for changes that affect the
firmware or its interfaces. Keep board wiring in the BSP, vendor APIs in the
matching platform HAL or peripheral driver, and update the relevant wiki page
when behavior changes.
