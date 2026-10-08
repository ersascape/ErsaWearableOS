# Ersa Wearable 0.4.4

OTA maintenance now deinitializes the BLE host after disconnecting the phone
and stopping the Apple protocol worker. This releases Bluedroid and GATT heap
allocations before Wi-Fi performs the HTTPS certificate-chain verification
that previously failed with an RSA big-number allocation error. The BLE GATT
server and security state are recreated after the update check or install.

The Arduino BLE client and characteristic pointers are cleared before the
vendor library deletes its objects. Bonding data in NVS is left untouched, and
the BLE controller memory remains eligible for reinitialization.

Both Terra platforms are built from the same source:

- XIAO ESP32-C3: `firmware-terra-xiao_esp32c3.bin`
- XIAO ESP32-C6: `firmware-terra-xiao_esp32c6.bin`

The device was not flashed during this change.
