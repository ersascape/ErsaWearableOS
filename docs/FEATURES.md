# Ersa Wearable Platform Feature Catalog

This catalog describes features in the current repository source. It
distinguishes implemented behavior from planned integrations and calls out
areas that still need measurements on the watch. Release tags define the exact
contents of a published firmware build.

## Watch apps and interaction

- Word-based Pebble-inspired clock face with date and configurable 12/24-hour
  format.
- Two-button app drawer and navigation, with app lifecycle and input routing.
- Monthly calendar, daily agenda, and task checklist.
- On-demand Wi-Fi hotspot and captive portal for network and CalDAV settings.
- Status/diagnostics app, notification history, incoming-call UI, and Now
  Playing UI.
- Monochrome 200×200 e-paper rendering with partial updates where supported.
  The host preview script uses the production UI renderers and ImageMagick to
  render notification, call, and media examples.

## Time, network, and organizer

- DS3231 real-time clock with timezone offset and 12/24-hour preferences.
- Pluggable time inputs: Apple BLE Current Time Service and Wi-Fi network time.
  Companion time is preferred over network time; network sync uses SNTP with
  HTTP Date fallback for networks that block UDP time requests.
- Wi-Fi credentials and application preferences stored in non-volatile
  settings.
- CalDAV event and task retrieval, daily agenda view, supported recurring event
  expansion, on-watch task completion, and persisted cache.
- Hotspot portal supports configuring Wi-Fi and CalDAV account details. Network
  and time synchronization are initiated by user action.

## Bluetooth and companion data

- Apple ANCS notification delivery with fragmented attribute assembly, UID
  matching, a bounded notification pipeline, and recovery when an attribute
  response is lost.
- Notification dismissal always removes the item locally. A remote iOS action
  is sent only when the notification advertises an action clearly labeled
  “Dismiss” or “Clear.”
- Caller details and incoming-call actions when supplied by Apple notification
  services. ANCS does not provide a universal dialer or complete call-state
  interface.
- Apple AMS track metadata, playback state, and supported playback commands.
- BLE advertising restart/retry handling and optional CTS/AMS/ANCS discovery.
- `IBluetooth` isolates BLE link/advertising transport. `ICompanionSource`
  isolates provider identity, availability, capabilities, normalized events,
  and semantic commands. The current ESP32 board composes Apple services behind
  these contracts; host tests also exercise a provider independent of the BLE
  transport.
- Android companion support and Linux MPRIS are extension targets, not part of
  this release.

## Power and battery

- ESP-IDF power management with FreeRTOS tickless idle and Bluetooth modem
  sleep configuration, plus inactivity-based automatic light-sleep policy.
- DVFS uses ESP-IDF power-management locks for interactive and compute scopes;
  clock transitions can be recorded in diagnostic logs.
- USB `ewctl` can temporarily force the CPU to 40, 80, or 160 MHz to compare
  behavior, then restore the configured 40–160 MHz automatic range. The forced
  setting is volatile and intended for development experiments.
- Battery voltage is sampled through the board ADC and mapped to an estimated
  percentage; low-battery handling is included. This board has no fuel gauge or
  current monitor, so percentage and runtime are estimates and require physical
  measurement for validation.

## Developer and release tooling

- `ewctl` over USB Serial/JTAG can query firmware/system, battery, BLE, power,
  and a bounded log ring. It also supports temporary CPU-frequency overrides,
  CSV power logging, redacted bug-report bundles, checksum-verified release
  flashing, and decoding flash-backed ESP-IDF panic coredumps with the matching
  ELF.
- `make test` runs host-side protocol and service tests; `make firmware` builds
  the ESP32-C3 firmware.
- GitHub Actions builds app and factory images for tagged releases and attaches
  checksums and flashing instructions.
- Tagged releases also include an Arch `ewctl` package and a pacman repository
  database. The package-only repository is hosted at
  `https://pkgs-wearables.ersa.dev/`; add this configuration, then install with
  `sudo pacman -Syu ewctl`:

  ```ini
  [ewctl]
  SigLevel = Optional
  Server = https://pkgs-wearables.ersa.dev/
  ```

  The package can also be built locally from `packaging/arch/ewctl/PKGBUILD`.
  Packages are currently unsigned; pacman downloads them and the repository
  database over HTTPS. Tagged releases attach the package and matching
  `ersa-ewctl.db` and `ersa-ewctl.files` assets. Build locally with `makepkg -si`
  if preferred.
- Each board's `DeviceInfo` supplies its display name, codename, and
  manufacturer. `ewctl status` also reports the compiled firmware version;
  BLE advertising and OTA use the selected
  BSP's properties. OTA stores the latest manifest at
  `ota/<codename>/ota.json` and images at `firmware/<codename>/<tag>.bin`;
  Terra (`Ampere Terra`) currently publishes at `ota/terra/ota.json`.
- The OTA service checks a compact HTTPS manifest, verifies release metadata
  and SHA-256, downloads in resumable HTTP ranges with bounded retry/backoff,
  and installs to the inactive app slot. OTA logs expose the current phase,
  byte progress, and transport or validation failures. Bootloader rollback
  protects the previous image until the new firmware confirms startup. Existing
  watches require one USB migration using the release bootloader, partition
  table, OTA metadata, and app image; the script preserves NVS settings. New
  devices can be factory flashed directly into the A/B layout.

## Useful commands

Run these from the repository root. Close any other program using the serial
port first.

```sh
python3 scripts/ewctl.py --port /dev/ttyACM0 status
python3 scripts/ewctl.py --port /dev/ttyACM0 power cpu-freq-get
python3 scripts/ewctl.py --port /dev/ttyACM0 power get-dvfs-state
python3 scripts/ewctl.py --port /dev/ttyACM0 ota
python3 scripts/ewctl.py --port /dev/ttyACM0 power cpu-freq-set 40
python3 scripts/ewctl.py --port /dev/ttyACM0 power cpu-freq-set 0
python3 scripts/ewctl.py --port /dev/ttyACM0 logs --follow
python3 scripts/ewctl.py --port /dev/ttyACM0 power log --interval 30 --duration 3600
python3 scripts/ewctl.py --port /dev/ttyACM0 debug bundle
python3 scripts/ewctl.py --port /dev/ttyACM0 flash release 0.1.3
python3 scripts/ewctl.py --port /dev/ttyACM0 debug coredump
```

`power` reports the measured CPU frequency and `cpu_test_override_mhz` (`0`
means automatic scaling). `power cpu-freq-set` accepts only 40, 80, 160, or 0;
it acknowledges and reboots in a controlled way so ESP-IDF applies the profile
before BLE and peripheral locks start. Zero restores automatic power
management. The requested profile survives software restart in RTC memory.
The 40 MHz profile can rise to 80 MHz while BLE holds its APB lock. Run
`power cpu-freq-get` after reboot to confirm the measured clock. Rich tables are
the default; add `--json` for machine-readable output.

New firmware saves panic coredumps in the reserved flash partition. Decode them
with `debug coredump` before another panic overwrites the saved dump; see the
[USB control guide](ewctl-control-bridge.md) for ELF matching and setup.

## Known validation limits

The firmware build and host test suite validate compilation and portable logic.
Power consumption, sleep residency, battery runtime, and forced-frequency radio
behavior need measurements on the assembled device. Apple features require an
iPhone with the relevant BLE services enabled; future Android/MPRIS sources are
not implemented.
