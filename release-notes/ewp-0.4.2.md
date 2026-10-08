# Ersa Wearable 0.4.2

This maintenance release fixes TLS trust failures during OTA and network time
sync by shipping ESP-IDF's complete default CA certificate bundle. The previous
reduced bundle did not contain every authority needed by the hosted package
endpoints.

App navigation uses the custom T1E partial waveform rather than a full panel
waveform. Full waveforms remain reserved for boot cleanup, day changes, and the
display manager's ghost-clearing cadence. E-paper partial updates still cause a
brief visible panel update when screen content changes.

Firmware is built for both Ampere Terra platforms:

- XIAO ESP32-C3: `firmware-terra-xiao_esp32c3.bin`
- XIAO ESP32-C6: `firmware-terra-xiao_esp32c6.bin`

The complete CA bundle increases firmware size. The C6 image is close to the
current OTA partition limit, so its packaged image size is checked in CI.
