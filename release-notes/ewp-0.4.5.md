# Ersa Wearable 0.4.5

This release adds a one-time partition migration path to the browser firmware
flasher for both XIAO ESP32-C3 and XIAO ESP32-C6. The migration writes the
matching bootloader, dual-slot partition table, OTA boot data, and firmware,
and verifies every downloaded image before writing. It preserves NVS settings
and repurposes the legacy SPIFFS region for the second OTA slot.

The browser flasher now handles USB serial ports that reject DTR/RTS reset
signals. It falls back to a manual BOOT/RESET sequence and reports successful
firmware writes separately from a reset signal failure.

Firmware artifacts are built for both Terra targets:

- XIAO ESP32-C3: `firmware-terra-xiao_esp32c3.bin`
- XIAO ESP32-C6: `firmware-terra-xiao_esp32c6.bin`

No device was flashed as part of preparing this release.
