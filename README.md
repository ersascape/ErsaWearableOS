# Ersa Wearable Platform

Ersa Wearable Platform (EWP) is an open source smartwatch firmware and embedded
application framework. The current firmware target is **Ampere Terra**, built
with a Seeed XIAO ESP32-C3, a 1.54-inch monochrome e-paper display, a DS3231
RTC, two buttons, and a LiPo battery monitor.

The code is organized around portable services and hardware contracts. The
selected board supplies `ersa::board::Board::current()` at startup; its
manufacturer, platform, and codename live under `src/bsp/`. Apps use interfaces
such as `IDisplay`, `IBluetooth`, `IWifiRadio`, and `IPowerManagement`, while
ESP32 and peripheral drivers stay behind those interfaces.

## What works today

- Pebble inspired word clock, app drawer, calendar, agenda, task list, status,
  notifications, incoming call, and Now Playing screens.
- Partial refresh on the 200×200 e-paper display, button input, DS3231 time,
  battery voltage reporting, and Bluetooth Low Energy connectivity.
- Apple ANCS notifications, AMS media metadata and supported playback controls,
  and Current Time Service time sync.
- Wi-Fi configuration portal, SNTP with HTTP time fallback, and CalDAV event
  and task sync.
- USB developer controls through `ewctl` for status, logs, power telemetry,
  firmware updates, and support bundles.

Android companion support, Linux MPRIS, and audio hardware are not implemented.
The feature catalog describes the current code and calls out planned work.

## Build and validate

Requirements: Python 3, PlatformIO, and a C++17 compiler. The repository
provides a PlatformIO bootstrap script for firmware builds.

```sh
make test
make firmware
```

The firmware binary is written to
`.pio/build/ErsaWearable/firmware.bin`. For USB flashing, device control, or
release firmware, see the guides below. Device-specific flash commands and
release publishing should use the matching firmware artifacts and partition
layout.

## Documentation

- [Published wiki](https://pkgs-wearables.ersa.dev/wiki/), deployed from `master` to GitHub Pages on every successful workflow run.
- [Wiki home](docs/index.md): start here for the developer and hardware guides.
- [Architecture and HAL boundaries](docs/HAL_ARCHITECTURE.md)
- [Adding a board](docs/ADDING_A_BOARD.md)
- [Feature catalog](docs/FEATURES.md)
- [Power and battery design](docs/POWER_AND_BATTERY_DESIGN.md)
- [Firmware power audit](docs/FIRMWARE_POWER_AUDIT.md)
- [Apple connectivity](docs/apple-connectivity.md)
- [USB control bridge and `ewctl`](docs/ewctl-control-bridge.md)
- [PCB pin map](docs/pcb-pin-map.md)
- [C++ API reference setup](docs/index.md#api-reference), generated with Doxygen

Build the documentation site and API reference locally with:

```sh
python3 -m pip install -r requirements-docs.txt
make docs
```

The result is `.pio/wiki-site/`, including `api/index.html`. See
[`Doxyfile`](Doxyfile) for source coverage and [`mkdocs.yml`](mkdocs.yml) for
the wiki navigation. The workflow publishes this same output to the repository's
GitHub Pages site under `/wiki/`, alongside the package and OTA files.

## Repository map

```text
include/ersa/       Public contracts, framework, services, and portable types
src/bsp/            Selected board composition and board pin maps
src/hal/            MCU and operating-system adapters
src/drivers/        Peripheral-specific driver implementations
src/ersa/           Application framework and service implementations
src/apps/           Watch applications
src/ui/             Watch UI and drawing support
tests/              Host-side platform and protocol tests
docs/               Wiki source pages and engineering guides
```

## Contributing

Run the platform tests and target firmware build for changes that affect the
firmware. Keep board wiring in its BSP, keep vendor APIs in the matching HAL or
peripheral driver, and update the relevant docs when behavior or interfaces
change. See [Adding a board](docs/ADDING_A_BOARD.md) and
[Architecture and HAL boundaries](docs/HAL_ARCHITECTURE.md).

The project is licensed under the MIT License; see [LICENSE](LICENSE).
