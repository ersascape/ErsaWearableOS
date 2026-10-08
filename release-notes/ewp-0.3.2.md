# Ersa Wearable Platform ewp-0.3.2

This release fixes the OTA ranged-transfer failure. The HTTP response callback
and downloader now share the same `Content-Range` record, so valid partial
responses from the published firmware host pass range validation.

The `ewctl version` command reports the CLI release, six-character Git hash,
and source branch. The Arch package builder always checks out the current
`master` source and embeds the hash of that master commit, even when a firmware
tag triggers the workflow.

Platform tests and the ESP32-C3 firmware build pass. The device was not flashed
as part of release preparation.
