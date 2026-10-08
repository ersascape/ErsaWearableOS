# Ersa Wearable 0.4.0

This release adds a custom SSD1681 display driver and DS3231 RTC driver behind
the existing `IDisplay` and `IRtc` contracts. Both drivers are composed by the
Terra BSP. RTClib and GxEPD2 are removed; the first party calendar type replaces
RTClib's app-facing date value. Adafruit GFX remains inside the display backend
for canvas rasterization and font compatibility.

Firmware is built for both Ampere Terra platforms:

- XIAO ESP32-C3: `firmware-terra-xiao_esp32c3.bin`
- XIAO ESP32-C6: `firmware-terra-xiao_esp32c6.bin`

Each platform includes its own factory image, boot components, flashing guide,
and SHA-256 manifest. The OTA manifest and firmware URL are platform-specific.
Use `ewctl flash release 0.4.0 --platform xiao_esp32c6` for the C6 release
image; C3 remains the default. One-time OTA migration accepts the same
`--platform` option.

The display retains the custom T1E V2a-derived waveform and current full-refresh
cadence. A periodic full refresh clears accumulated partial-update ghosting.
