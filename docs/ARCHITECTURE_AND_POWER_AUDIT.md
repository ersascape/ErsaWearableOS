# Architecture and power audit — 2026-10-08

This replaces the dated 2026-09-29 audit with a review of the current source
tree, including the runtime scheduler and e-paper idle policy. It is a source
and build-configuration review, not a measurement of
battery life or a claim that the firmware has been validated on hardware.

## Summary

The code has the main layers needed for a portable wearable: board composition
in the BSP, device-independent contracts in `include/ersa/hal/`, service and
application policy above those contracts, and ESP32 implementations under
`src/hal/esp32/`. The runtime currently composes one board and its services in
`src/ui/watch_ui.cpp`. It is not yet a fully uniform, independently replaceable
HAL architecture: some legacy `src/core/` code and application code still
depend on Arduino-facing interfaces, and the UI loop is also the central event
executor and sleep scheduler.

Power-management Kconfig enables ESP-IDF PM, FreeRTOS tickless idle, and
Bluetooth modem sleep. The ESP32 power HAL configures automatic light sleep,
holds an `ESP_PM_NO_LIGHT_SLEEP` lock while active, and releases it after five
seconds without button activity when all other blockers are clear. The UI waits
for the earliest service or application deadline; GPIO and BLE notifications
wake it between deadlines. These facts establish that the firmware is
configured to attempt coordinated BLE-compatible sleep; they do not establish
sleep residency, reconnect quality, or battery endurance on a physical watch.

## Findings

### 1. Power policy has two disconnected sleep-eligibility paths

**Resolved in the current source:** `WatchUi::tick()` now includes
`PowerManager::canSleep()` in its automatic-sleep eligibility condition, so a
held feature lease keeps the platform no-sleep guard active. Repeated
acquisitions of the same tag increment a reference count; each release removes
one lease. Host tests cover overlapping same-tag leases. Keep new background
work inside a scoped lease or pair raw acquisition/release on every path.

### 2. The UI loop owns too many runtime responsibilities

`WatchUi::tick()` still polls input, dispatches queued events, ticks services,
applies screen timeouts, and blocks for wake notifications. The first
separation step is implemented in `runtime::SleepEligibility` and
`WakeDeadlineSet`: the former combines blockers, and the latter chooses the
earliest delay without allocation. UI rendering and product timeout decisions
remain in the loop, so this is a reduction in scattered sleep logic rather
than a full runtime-executor extraction.

The current wake plan covers minute changes, the BLE manager's retry deadline,
battery sample due time, watchface media debounce, the 15-second boot time
fallback, and the 60-second transient-screen return. Deadlines are still
collected by `WatchUi`; moving them into service-owned deadline APIs remains
follow-up work. New periodic or retrying work must register a deadline or it
may be delayed while the UI task waits.

### 3. Manual chip sleep entry remains exposed

**Resolved in the current source:** direct light-sleep entry has been removed
from the service and HAL contracts. Normal idle sleep uses `waitForWake()` and
FreeRTOS automatic light sleep, allowing ESP-IDF to coordinate registered
locks and RTOS deadlines. Deep sleep remains available for the qualified
critical-battery shutdown path.

### 4. Device-independent contracts exist, but legacy boundaries remain

Display, input, RTC, battery, Wi-Fi, Bluetooth, companion-source, power, and
console contracts are defined under `include/ersa/hal/`. ESP32-specific GPIO,
ADC, OneButton, DS3231/Wire, and USB Serial/JTAG implementations live under
`src/hal/esp32/`; the BSP provides pin numbers, pull/active levels, and battery
divider scaling. `WatchClock` remains a compatibility facade whose public
The app-facing calendar value is now first party and RTClib has been removed.
Network sync, settings, diagnostics, and OTA still have direct platform-library usage. Keep moving those behind network,
storage, diagnostics, and OTA HAL contracts.

### 5. E-paper retains its image while its drive voltage is off

The custom SSD1681 driver disables panel drive voltage after a refresh while the e-paper
image remains visible. `DisplayManager` owns the 360-partial-frame full-waveform
limit and refresh rate limit. The manager does not send a redundant idle
power-off or power-on request. The UI still identifies dirty bounds and forces
a full waveform on the first frame, day change, or app transition. Movement
within the current app keeps
using partial windows. BUSY completion remains a driver concern and is not
coupled to button polling. This removes an incorrect manager power-state
assumption; it does not reduce the panel's
refresh-waveform duration.

### 6. Battery state is an estimate, not fuel-gauge telemetry

The ESP32 battery HAL measures the board's ADC divider and derives voltage and
a percentage estimate. The current board does not expose a dedicated fuel
gauge, charger status, current measurement, or temperature measurement to the
firmware. Treat percentage as a voltage-based estimate and charging state as
unknown unless the hardware supplies a real signal. Calibrate ADC voltage on
the assembled board and test low-voltage behavior under radio and display load.

### 7. Sleep and BLE behavior need hardware evidence

The configured PM and modem-sleep options plus `esp_pm_configure()` establish
the intended automatic light-sleep path. Software logs that say sleep is
“armed” report policy setup only. Validate actual light-sleep residency and
BLE link stability together while connected, including notification bursts,
advertising restart, button wake latency, and display refresh. Measure current
at the battery connection with USB disconnected; a host build or test cannot
measure these properties.

## Recommended work order

1. Move deadline ownership from `WatchUi` into the services that own the
   deadlines, preserving the tested fixed-capacity scheduler.
2. Move dirty-region and full-waveform requests behind a display refresh
   policy API while retaining per-app invalidation bounds.
3. Move remaining raw Arduino/ESP-IDF calls in network sync, USB control, and
   OTA behind platform diagnostics and transport contracts.
4. Collect on-device BLE, residency, wake-latency, and battery-current
   measurements before claiming power savings.

## Review limits

This audit did not flash hardware or measure power. Hardware validation must
record firmware revision, battery voltage, USB state, BLE connection state and
negotiated parameters, sleep-mode residency, wake source, and average current
for each scenario. Re-run the audit when the scheduler, BSP composition, or
power HAL changes materially.
