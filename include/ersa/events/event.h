#pragma once

#include <stdint.h>
#include <stddef.h>

namespace ersa {
namespace events {

/**
 * Normalized system-event identifiers used by the event bus.
 * Events describe intent and state changes, not the chipset callback that
 * produced them, so services and apps can share the same code across boards.
 */
enum class EventType : uint16_t {
    None = 0,
    Boot,
    Suspend,
    Resume,

    ButtonPressed,
    ButtonReleased,
    ButtonClicked,
    ButtonDoubleClicked,
    ButtonLongPressed,

    MinuteTick,
    SecondTick,
    TimeSync,

    BatteryChanged,
    BatteryLow,

    NetworkConnected,
    NetworkDisconnected,

    BleConnected,
    BleDisconnected,
    CompanionConnected,
    CompanionDisconnected,

    CallIncoming,
    CallAccepted,
    CallRejected,
    CallEnded,

    MediaTrackChanged,
    MediaStateChanged,

    NotificationReceived,
    NotificationRemoved,
    NotificationsCleared,
    Custom
};

/** Product-relative button identity independent of GPIO numbering. */
enum class ButtonId : uint8_t {
    Unknown = 0,
    Button1, // Top button (S2 on Ampere Works T1E)
    Button2  // Bottom button (S1 on Ampere Works T1E)
};

/** Provenance used by TimeService to resolve clock-update priority. */
enum class TimeSource : uint8_t {
    Unknown = 0,
    Network,
    BleCurrentTime,
    Companion,
    Manual
};

/** Button identity carried by press, release, click, and hold events. */
struct ButtonPayload {
    /** Logical button key, not its physical GPIO number. */
    ButtonId button{ButtonId::Unknown};
};

/** Calendar fields and source metadata carried by clock events. */
struct TimePayload {
    /** Unix epoch seconds using the firmware's local-wall-time convention. */
    uint32_t epoch{0};
    uint16_t year{2026};
    uint8_t month{1};
    uint8_t day{1};
    uint8_t hour{0};
    uint8_t minute{0};
    uint8_t second{0};
    TimeSource source{TimeSource::Unknown};
};

/** Battery sample shared by battery-changed and low-battery events. */
struct BatteryPayload {
    uint16_t millivolts{0};
    uint8_t percentage{100};
    bool connected{false};
    bool charging{false};
};

/** Station-network state carried by connection transition events. */
struct NetworkPayload {
    bool connected{false};
};

/** Normalized call identity and state; strings are bounded inline copies. */
struct CallPayload {
    char caller[32];
    char number[20];
    uint8_t state; // 0: Incoming, 1: Active, 2: Ended
};

/** Current companion media metadata and playback state. */
struct MediaPayload {
    char title[32];
    char artist[32];
    bool playing;
};

/** Notification data copied into the event so provider buffers may be released. */
struct NotificationPayload {
    char title[32];
    char message[64];
    char app[20];
    uint32_t uid;
    bool canDismissRemotely;
};

/**
 * Fixed-size event envelope with a tagged, type-specific payload union.
 *
 * Creating and posting an Event copies all bounded text into the value. This
 * lets asynchronous producers release callback-owned buffers immediately and
 * avoids heap allocation; consumers must read only the union member associated
 * with `type` because the payload storage is shared.
 */
struct Event {
    /** Discriminator selecting the active payload member. */
    EventType type{EventType::None};
    /** Monotonic device uptime at event creation, in milliseconds. */
    uint32_t timestampMs{0};

    union {
        ButtonPayload button;
        TimePayload time;
        BatteryPayload battery;
        NetworkPayload network;
        CallPayload call;
        MediaPayload media;
        NotificationPayload notification;
        void* customPayload;
    };

    /** Construct an empty event whose button payload is initialized safely. */
    Event() : type(EventType::None), timestampMs(0) {
        button.button = ButtonId::Unknown;
    }

    /** Construct an event envelope for `t` at an optional monotonic timestamp. */
    explicit Event(EventType t, uint32_t ts = 0) : type(t), timestampMs(ts) {
        button.button = ButtonId::Unknown;
    }

    /** Build a button event with the requested subtype and logical button key. */
    static Event createButton(EventType t, ButtonId btn, uint32_t ts = 0) {
        Event e(t, ts);
        e.button.button = btn;
        return e;
    }

    /** Build a minute-boundary event containing one RTC snapshot. */
    static Event createMinuteTick(const TimePayload& tp, uint32_t ts = 0) {
        Event e(EventType::MinuteTick, ts);
        e.time = tp;
        return e;
    }

    /** Build a request to apply an external time value through TimeService. */
    static Event createTimeSync(uint32_t epoch, TimeSource source, uint32_t ts = 0) {
        Event e(EventType::TimeSync, ts);
        e.time.epoch = epoch;
        e.time.source = source;
        return e;
    }

    /** Build a battery sample event, copying voltage, estimate, and status bits. */
    static Event createBatteryChanged(uint16_t mv, uint8_t pct, bool conn, bool chg, uint32_t ts = 0) {
        Event e(EventType::BatteryChanged, ts);
        e.battery.millivolts = mv;
        e.battery.percentage = pct;
        e.battery.connected = conn;
        e.battery.charging = chg;
        return e;
    }

    /**
     * Build a call transition with bounded copies of caller and number.
     * Text is truncated to the inline payload capacity and null-terminated so
     * later asynchronous consumers do not retain borrowed provider pointers.
     */
    static Event createCall(EventType t, const char* caller, const char* number, uint8_t state, uint32_t ts = 0) {
        Event e(t, ts);
        e.call.state = state;
        e.call.caller[0] = '\0';
        e.call.number[0] = '\0';
        if (caller) {
            for (size_t i = 0; i < sizeof(e.call.caller) - 1 && caller[i]; ++i) {
                e.call.caller[i] = caller[i];
                e.call.caller[i + 1] = '\0';
            }
        }
        if (number) {
            for (size_t i = 0; i < sizeof(e.call.number) - 1 && number[i]; ++i) {
                e.call.number[i] = number[i];
                e.call.number[i + 1] = '\0';
            }
        }
        return e;
    }

    /** Build a media-track event with copied title, artist, and playback state. */
    static Event createMedia(const char* title, const char* artist, bool playing, uint32_t ts = 0) {
        Event e(EventType::MediaTrackChanged, ts);
        e.media.playing = playing;
        e.media.title[0] = '\0';
        e.media.artist[0] = '\0';
        if (title) {
            for (size_t i = 0; i < sizeof(e.media.title) - 1 && title[i]; ++i) {
                e.media.title[i] = title[i];
                e.media.title[i + 1] = '\0';
            }
        }
        if (artist) {
            for (size_t i = 0; i < sizeof(e.media.artist) - 1 && artist[i]; ++i) {
                e.media.artist[i] = artist[i];
                e.media.artist[i + 1] = '\0';
            }
        }
        return e;
    }

    /**
     * Build a notification event with bounded text copies and remote-dismiss
     * metadata. Fixed buffers make queued events self-contained and predictable.
     */
    static Event createNotification(const char* title, const char* message, const char* app = "", uint32_t uid = 0, uint32_t ts = 0, bool canDismissRemotely = false) {
        Event e(EventType::NotificationReceived, ts);
        e.notification.uid = uid;
        e.notification.canDismissRemotely = canDismissRemotely;
        e.notification.title[0] = '\0';
        e.notification.message[0] = '\0';
        e.notification.app[0] = '\0';
        if (title) {
            for (size_t i = 0; i < sizeof(e.notification.title) - 1 && title[i]; ++i) {
                e.notification.title[i] = title[i];
                e.notification.title[i + 1] = '\0';
            }
        }
        if (message) {
            for (size_t i = 0; i < sizeof(e.notification.message) - 1 && message[i]; ++i) {
                e.notification.message[i] = message[i];
                e.notification.message[i + 1] = '\0';
            }
        }
        if (app) {
            for (size_t i = 0; i < sizeof(e.notification.app) - 1 && app[i]; ++i) {
                e.notification.app[i] = app[i];
                e.notification.app[i + 1] = '\0';
            }
        }
        return e;
    }
};

} // namespace events
} // namespace ersa
