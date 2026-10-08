# Ersa Wearable 0.4.1

This display maintenance release makes the Terra SSD1681 driver force a full
waveform at least every five minutes, even when the UI requested a small
partial update. This clears residual pigment accumulated during long runs of
partial updates. The cadence remains inside the display driver and is
invisible to apps and HAL clients.

Top-button HOME handling is consistent across apps: holding B1 returns to the
app drawer, and the drawer's Clock item returns to the watchface. The OneButton
adapter now emits the double-click events already defined by the input
contract, with a 250 ms double-click window.

The release is built for both Terra platforms:

- XIAO ESP32-C3: `firmware-terra-xiao_esp32c3.bin`
- XIAO ESP32-C6: `firmware-terra-xiao_esp32c6.bin`

Select the matching platform when using `ewctl flash release 0.4.1`.
