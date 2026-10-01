# Ersa Wearable Platform ewp-0.2.1

This release improves BLE recovery and adds diagnostics for the DS3231 clock.

- Unexpected advertising stops now schedule a retry. Authentication results are
  retained until Apple GATT discovery is ready, and failed authentication
  disconnects the link so advertising can resume.
- The watch checks the DS3231 oscillator-stop flag. When set, it preserves the
  software clock instead of repeatedly copying a stale RTC value. A trusted time
  sync clears the flag.
- Once-a-minute diagnostics compare RTC time with the software clock. The clock
  cache is also refreshed from the DS3231 after light-sleep wakeups.

Existing devices already migrated to the A/B partition layout can install this
release from the on-watch System Update app. New devices can use the factory
image attached to this release.
