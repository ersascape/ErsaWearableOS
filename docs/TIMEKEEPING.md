# Timekeeping and clock recovery

The DS3231 RTC supplies wall time immediately at boot. The `BOOT CHECK
rtc=ok` message means the chip responds and does not report oscillator stop; it
does not prove that the displayed time is accurate.

If no phone or manual time source has synchronized the clock within 15 seconds
of UI startup, firmware starts a background network fallback when saved Wi-Fi
credentials are available. It makes up to two bounded Wi-Fi/NTP attempts,
with HTTP time endpoints as an NTP fallback. The display and BLE event loop
continue while this runs. A completed timestamp is applied from the main loop
to keep RTC access serialized.

Time sources have priority: manual adjustment, then phone/companion time, then
network time. Thus a phone's Current Time Service update can correct the RTC
after boot and can replace a network result that arrived first. A network
result cannot override a phone or manual adjustment. If Wi-Fi is not configured
or both attempts fail, the watch keeps running from the RTC and can still
correct time when a phone connects or the user syncs manually.

The RTC battery and oscillator should still be checked if time is wrong after
repeated successful synchronization. The `RTC adjusted` log confirms a source
changed the chip; it does not validate the backup cell or long-term drift.
