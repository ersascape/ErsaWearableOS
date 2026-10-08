# Ersa Wearable Platform ewp-0.3.0

This release improves the platform boundaries, runtime scheduling, e-paper
behavior, RTC diagnostics, and developer documentation.

- Move board composition to `src/bsp/<manufacturer>/<platform>/<codename>/` and
  provide board pin configuration to the platform HALs. Display, input, power,
  RTC, battery, network, portal, storage, console, and diagnostics now have
  clearer contracts and adapters.
- Centralize UI wait decisions in a fixed-capacity scheduler. After five
  seconds without button activity, automatic light sleep is allowed when USB,
  foreground work, network sync, display activity, wake locks, and pending work
  do not block it. GPIO interrupts and queued BLE work wake the UI task; it can
  wait for the next real deadline instead of polling every 10 ms.
- Correct e-paper idle handling. The panel driver disables drive voltage after
  refresh while retaining the visible image. The display manager no longer
  issues a redundant idle power-off or controller reset. Display work receives
  a scoped performance lock.
- Verify RTC writes, report I²C timeout context, apply timezone settings, and
  resynchronize the software clock from the DS3231 after automatic light sleep.
- Reduce ANCS and DVFS log volume. Normal notification UIDs and performance
  scope acquisitions/releases are no longer printed individually; dropped
  notification records are summarized after a burst.
- Expand the C++ app SDK, Doxygen API reference, power/runtime documentation,
  and GitHub Pages wiki deployment. Add USB provisioning and RTC diagnostics
  to `ewctl`, with grouped `config wifi|caldav|time|hotspot set` commands.

## Upgrade and validation

Devices already using the A/B partition layout can install the release through
the on-watch System Update app or `ewctl`. Use the attached factory image for a
new device or one that has not been migrated to the A/B layout. Read the
included flashing guide before using a factory image.

Platform tests and the ESP32-C3 firmware build pass. Automatic light-sleep
residency, BLE stability, button wake latency, and battery current still need
measurement on the physical watch with USB disconnected. An attached USB
console intentionally prevents automatic light sleep.
