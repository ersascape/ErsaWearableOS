#pragma once

#include "ersa/hal/bluetooth.h"
#include "ersa/hal/companion_source.h"
#include "ersa/events/event_bus.h"
#include "ersa/common/types.h"

namespace ersa {
namespace services {

/** Normalized telephony state exposed to apps, independent of HFP/ANCS source. */
enum class CallState : uint8_t {
    Idle = 0,
    Incoming,
    Active,
    Ended
};

/** Bounded call-history record retained for the recent-call UI. */
struct RecentCall {
    /** Display name supplied by the companion, possibly truncated. */
    char name[32];
    /** Dialable remote number supplied by the provider, possibly truncated. */
    char number[20];
    /** Local epoch seconds when this call record was captured. */
    uint32_t timestampEpoch;
};

/** Bounded notification model copied out of provider callback memory. */
struct AppNotification {
    char title[32];
    char message[64];
    char app[20];
    uint32_t uid;
    uint32_t timestampEpoch;
    bool canDismissRemotely;
};

/**
 * Normalizes companion events and commands over separate transport/source HALs.
 *
 * The manager is the app-facing semantic layer: it copies callback strings,
 * retains bounded recent-call/notification state, validates provider
 * capabilities, and publishes normalized events. Transport link state and
 * companion readiness remain separate because an encrypted BLE connection can
 * exist before ANCS/AMS/CTS discovery finishes.
 */
class BluetoothManager {
public:
    using WakeCallback = void (*)(void* user);
    /// Return the installed process-wide Bluetooth manager.
    static BluetoothManager& instance();
    /// Install the process-wide Bluetooth manager.
    static void setInstance(BluetoothManager* inst);

    /** Compose independently injected transport, provider, and event contracts. */
    BluetoothManager(hal::IBluetooth& ble, hal::ICompanionSource& source, events::EventBus& bus);

    /// Initialize the transport, source, and event callbacks.
    Result<void> init();
    /** Drain callbacks and run bounded provider/transport maintenance. */
    void tick();
    /**
     * Register the wake callback used when a radio worker enqueues new data.
     * The callback should notify a task only; it must not render UI directly.
     */
    void setWakeCallback(WakeCallback callback, void* user) { wakeCallback_ = callback; wakeUserData_ = user; }
    /** Check both provider availability and current dial capability. */
    bool canDial() const { return source_.isAvailable() && source_.capabilities().dial; }
    /** Check current hangup support rather than assuming all peers support it. */
    bool canHangup() const { return source_.isAvailable() && source_.capabilities().hangup; }
    /** Report feature readiness after provider discovery completes. */
    bool notificationsReady() const { return source_.isAvailable() && source_.capabilities().notifications; }
    /** Report feature readiness independently of BLE link state. */
    bool mediaReady() const { return source_.isAvailable() && source_.capabilities().media; }
    /// Return the active source's stable machine-readable identifier.
    const char* companionSourceId() const { return source_.sourceId(); }
    /// Return the active source's current capabilities.
    hal::CompanionCapabilities companionCapabilities() const { return source_.capabilities(); }
    /// Return whether the active source is available.
    bool companionSourceAvailable() const { return source_.isAvailable(); }
    /// Return BLE transport link state.
    bool bleConnected() const { return ble_.isConnected(); }

    /** Return link-up state; this is not equivalent to source readiness. */
    bool isConnected() const;
    /// Return BLE advertising state.
    bool isAdvertising() const { return ble_.isAdvertising(); }
    /** Forward transport maintenance deadline for low-power wait scheduling. */
    uint32_t nextWakeDelayMs(uint32_t nowMs) const { return ble_.nextWakeDelayMs(nowMs); }
    /// Return the local BLE device name.
    const char* getDeviceName() const;
    /// Return the local BLE address.
    const char* getDeviceAddress() const;
    /** Request the transport to restart advertising after disconnect/recovery. */
    void restartAdvertising();

    // Call state & telephony actions
    /// Return normalized call state.
    CallState getCallState() const { return callState_; }
    /// Return the current caller name, if provided by the source.
    const char* getCallerName() const { return currentCaller_; }
    /// Return the current caller number, if provided by the source.
    const char* getCallerNumber() const { return currentNumber_; }
    /// Return elapsed seconds for the current active call.
    uint32_t getCallDurationSec() const;

    /** Ask the provider to answer the current call; false means unsupported/rejected. */
    bool acceptCall();
    /** Reject the current incoming call when the provider supports the action. */
    bool rejectCall();
    /** End the current call through the provider; acceptance is not remote execution. */
    bool hangupCall();
    /**
     * Request dialing and optionally associate a display name locally.
     * @return True if the provider accepted the command for processing.
     */
    bool dial(const char* number, const char* name = nullptr);
    /** Dial one retained history record, returning false for an invalid index. */
    bool dialRecent(size_t index = 0);

    // Recent calls history
    /// Return the number of retained recent-call entries.
    size_t getRecentCallCount() const { return recentCount_; }
    /// Return a recent-call entry by index.
    const RecentCall& getRecentCall(size_t index) const;
    /** Insert or refresh a history record while respecting fixed history capacity. */
    void addRecentCall(const char* name, const char* number);

    // Media playback state & control
    /// Return whether the latest companion media state is playing.
    bool isPlaying() const { return mediaPlaying_; }
    /// Return the latest companion track title.
    const char* getMediaTitle() const { return mediaTitle_; }
    /// Return the latest companion artist name.
    const char* getMediaArtist() const { return mediaArtist_; }

    /// Request media playback.
    bool mediaPlay();
    /// Request media pause.
    bool mediaPause();
    /// Request media play/pause toggle.
    bool mediaToggle();
    /// Request the next media item.
    bool mediaNext();
    /// Request the previous media item.
    bool mediaPrevious();

    // Testing / Simulation hooks
    /// Inject an incoming call event for host simulation.
    void simulateIncomingCall(const char* name, const char* number);
    /// Inject media metadata and state for host simulation.
    void simulateMedia(const char* title, const char* artist, bool playing);
    /// Inject a notification event for host simulation.
    void simulateNotification(const char* title, const char* message, const char* app = "Messages");

    // Notifications history
    /// Return the number of retained notification entries.
    size_t getNotificationCount() const { return notifCount_; }
    /// Return a notification entry by index.
    const AppNotification& getNotification(size_t index) const;
    /**
     * Remove a retained notification and request remote dismissal if its
     * original provider session and capability still permit it.
     */
    bool dismissNotification(size_t index);
    /// Add or update a notification entry.
    void addNotification(const char* title, const char* message, const char* app, uint32_t uid, bool canDismissRemotely = false);
    /// Clear all retained notifications and dismissed UID state.
    void clearNotifications();

private:
    static void onSourceCall(hal::CompanionCallAction action, const char* caller, const char* number, void* user);
    static void onSourceMedia(bool playing, const char* title, const char* artist, void* user);
    static void onBleConnection(bool connected, void* user);
    static void onSourceAvailability(bool available, void* user);
    static void onSourceNotification(const char* title, const char* message, const char* app, uint32_t uid, bool canDismissRemotely, void* user);

    void receive(const events::Event& event);
    void apply(const events::Event& event);
#if defined(ARDUINO)
    void* incomingQueue_{nullptr};
    void* timeSyncQueue_{nullptr};
#endif
    WakeCallback wakeCallback_{nullptr};
    void* wakeUserData_{nullptr};
    hal::IBluetooth& ble_;
    hal::ICompanionSource& source_;
    events::EventBus& bus_;

    CallState callState_{CallState::Idle};
    char currentCaller_[32]{""};
    char currentNumber_[20]{""};
    uint32_t callStartMs_{0};

    static constexpr size_t MAX_RECENTS = 5;
    RecentCall recents_[MAX_RECENTS];
    size_t recentCount_{0};

    static constexpr size_t MAX_NOTIFS = 10;
    AppNotification notifications_[MAX_NOTIFS];
    size_t notifCount_{0};
    static constexpr size_t MAX_DISMISSED_UIDS = 16;
    uint32_t dismissedUids_[MAX_DISMISSED_UIDS]{};
    size_t dismissedCount_{0};

    bool mediaPlaying_{false};
    char mediaTitle_[32]{"No Media"};
    char mediaArtist_[32]{"Bluetooth Idle"};
    bool initialized_{false};
};

} // namespace services
} // namespace ersa
