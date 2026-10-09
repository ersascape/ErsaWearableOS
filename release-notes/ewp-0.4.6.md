# Ersa Wearable 0.4.6

The C3 and C6 browser partition migration is now published through a separate
browser-only manifest. The watch-facing OTA manifest remains schema 1 and
small enough for the firmware's bounded manifest reader, so the on-device
updater can continue to check for releases normally.

The browser flasher verifies the separate migration metadata and each image
before writing. Both board targets use the same dual-slot partition layout.

- XIAO ESP32-C3: `firmware-terra-xiao_esp32c3.bin`
- XIAO ESP32-C6: `firmware-terra-xiao_esp32c6.bin`

No device was flashed as part of preparing this release.
