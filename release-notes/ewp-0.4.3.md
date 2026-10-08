# Ersa Wearable 0.4.3

This maintenance release reduces TLS handshake memory pressure on the ESP32-C3
and ESP32-C6. The certificate bundle found the issuer in the reported failure;
the `0x4290` error is an RSA big-number allocation failure while verifying the
server chain. The TLS configuration now releases handshake-only certificate
and key data after use, and OTA diagnostics report free and largest-block heap
at the transport failure point.

The standard reduced ESP-IDF CA bundle remains enabled because the reported
issuer already matched. Shipping every root certificate would increase the
firmware image without fixing the RSA allocation failure.

App navigation uses the custom T1E partial waveform. Full waveforms remain
reserved for boot cleanup, day changes, and the display manager's ghost-clearing
cadence. Partial e-paper updates still visibly pulse the area being refreshed.

Firmware is built for both Ampere Terra platforms:

- XIAO ESP32-C3: `firmware-terra-xiao_esp32c3.bin`
- XIAO ESP32-C6: `firmware-terra-xiao_esp32c6.bin`
