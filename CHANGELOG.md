# Changelog

## ewp-0.3.1

- Keep a remotely dismissible notification in the watch history when the Apple
  companion cannot accept the dismissal request, so the user can retry.
- Report whether the ANCS dismissal was queued and whether the BLE worker
  submitted the action to the control point.

See [release notes](release-notes/ewp-0.3.1.md) for details.

## ewp-0.3.0

- Reorganized board composition under manufacturer/platform/codename, expanded
  board pinctrl contracts, and moved display, input, power, RTC, battery,
  network, portal, storage, console, and diagnostics access behind explicit
  HAL boundaries.
- Added a deadline-driven runtime scheduler. GPIO interrupts and service
  deadlines wake the UI task; idle waits no longer poll every 10 ms. Automatic
  BLE-coordinated light sleep becomes eligible after 5 seconds without button
  activity, provided USB, display, network, app, and feature-lease blockers are
  clear.
- Corrected e-paper idle handling: the driver releases panel drive voltage
  after refresh while the image remains visible; the display manager no longer
  issues a redundant idle power-off or controller reset. Display refreshes use
  a dedicated performance scope.
- Verified RTC writes and improved I²C timeout diagnostics, timezone handling,
  and clock reconciliation after light sleep.
- Reduced normal ANCS and DVFS log volume. Notification queue loss is summarized
  after bursts, while clock changes, recovery, and failures remain visible.
- Added USB provisioning and RTC diagnostics through `ewctl`, expanded the app
  SDK and C++ API documentation, added Doxygen coverage reporting, and publish
  the wiki/API reference through GitHub Pages.
- Added grouped USB provisioning commands: `config wifi set`,
  `config caldav set`, `config time set`, and `config hotspot set`; the flat
  `config set` form remains supported for existing scripts.

See [release notes](release-notes/ewp-0.3.0.md) for upgrade and validation
details.

## ewp-0.2.1

- Retry BLE advertising after unexpected radio stops, release stalled links after
  authentication failures, and deliver authentication results that arrive before
  the Apple GATT worker is ready.
- Detect the DS3231 oscillator-stop flag and retain the advancing software clock
  instead of replacing it with a stale RTC value; log once-a-minute RTC and
  software time readings to diagnose clock drift or a stopped oscillator.
- Re-anchor the clock from the DS3231 after light-sleep wakeups.

See [release notes](release-notes/ewp-0.2.1.md) for details.

## ewp-0.2.0

- Resume interrupted HTTPS OTA range downloads from the last written byte, with
  bounded retries and backoff for transient connection resets.
- Validate each HTTP range and the embedded image version before selecting the
  inactive slot; verify the complete image against the manifest SHA-256.
- Add OTA phase, byte-progress, transport, flash, validation, and slot-selection
  diagnostics, and show a retryable failure state in the updater.
- Reuse the Apple GATT service cache across reconnects to avoid the BLE
  descriptor teardown panic; replace the client after a peer or service change.
- Show the compiled firmware version in Status and `ewctl status`.

See [release notes](release-notes/ewp-0.2.0.md) for details.

## ewp-0.1.5

- Keep BLE suspended between a successful OTA check and installation to avoid
  reconnect heap fragmentation; leaving the updater resumes Bluetooth.
- Add a source-level maintenance pause/resume contract so OTA coordinates
  companion providers generically rather than knowing about Apple protocols.
- Lower OTA's contiguous-heap preflight for the flash-resident certificate-bundle
  TLS path while keeping a total free-heap guard.
- Reject malformed alternate-slot segment tables before invoking the ESP image
  verifier, avoiding noisy errors for interrupted/invalid OTA contents.
- Show current boot uptime on the watch's Status screen and format it in the
  human-readable `ewctl status` output.

See [release notes](release-notes/ewp-0.1.5.md) for details.

## ewp-0.1.4

- Suspend and tear down the BLE service during OTA checks and installs, then
  restore advertising when an operation exits without rebooting.
- Make OTA explicitly disconnect an active phone link before radio teardown,
  with a GATT-close fallback and diagnostic status logging.
- Use ESP-IDF's certificate bundle for OTA HTTPS validation to reduce TLS heap
  pressure.

See [release notes](release-notes/ewp-0.1.4.md) for details.

## ewp-0.1.3

- Added rollback-capable A/B slots, a one-time OTA partition migration, and the
  `system update` screen with manifest-based HTTPS update checks and installs.
- Added current/alternate slot state and bootability to `ewctl ota`, plus an
  on-demand ESP-IDF DVFS residency report.
- Changed the app drawer to four roomier entries without page counters and fixed
  Arch package runtime dependency names.

See [release notes](release-notes/ewp-0.1.3.md) for upgrade and asset details.

## ewp-0.1.2

- Split the companion-provider contract (`ICompanionSource`) from BLE transport
  (`IBluetooth`) so Apple, Android, and MPRIS integrations can be added as
  independent sources. Apple ANCS/AMS/CTS remains the implemented provider.
- Added a bounded USB Serial/JTAG control bridge and `ewctl` host CLI for system,
  battery, BLE, power, and log inspection.
- Added a temporary CPU-frequency test override: pin to 40, 80, or 160 MHz, or
  restore automatic 40–160 MHz power management with `cpu_mhz: 0`.
- Expanded battery sample reporting, diagnostic log access, architecture docs,
  and simulated notification/call/media previews.

See [release notes](release-notes/ewp-0.1.2.md) and the
[feature catalog](docs/FEATURES.md) for the full feature inventory and caveats.

## ewp-0.1.1

- Fixed ANCS attribute requests for notifications that advertise a negative
  action, restoring message and caller details from iOS.
- Made ANCS recovery skip a UID that returns no attribute response so one stale
  notification cannot block later notifications.
- Kept the watch eligible for light sleep during passive BLE notification
  bursts; buttons reset the 30-second inactivity timer.
- Improved Apple BLE discovery/recovery, call handling and recent-call controls;
  added BLE Current Time Service preference with network time fallback.
- Added low-battery safeguards and last-session uptime reporting.
- Updated the firmware build and release notes workflow for tagged binary
  releases.

See [release notes](release-notes/ewp-0.1.1.md) for the user-facing summary.
