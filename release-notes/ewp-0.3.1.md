# Ersa Wearable Platform ewp-0.3.1

This patch makes remote notification dismissal failures visible and retryable.
When the Apple companion cannot accept a dismissal request, the watch keeps the
notification in its history. Logs distinguish a queued request from an ANCS
action submitted by the BLE worker, including cases where the control point or
notification UID is no longer actionable.

The platform test suites and ESP32-C3 firmware build pass. No device was flashed
as part of this release preparation.
