#pragma once

#include "ersa/common/types.h"
#include <stdint.h>

namespace ersa {
namespace hal {

/**
 * Companion providers report normalized call actions independent of transport.
 * A provider may be ANCS over BLE, an Android protocol, MPRIS, or a host fake.
 */
enum class CompanionCallAction : uint8_t { Incoming = 0, Answered, Rejected, Ended };
/** Transport-neutral controls requested for companion media playback. */
enum class CompanionMediaAction : uint8_t { Play = 0, Pause, Toggle, Next, Previous, VolumeUp, VolumeDown };

/**
 * Provider callback function types.
 * String pointers are borrowed only for callback duration and must be copied
 * by consumers. Providers may invoke callbacks on a radio/worker thread, so
 * consumers should enqueue work rather than mutate UI state directly.
 */
using CompanionCallCallback = void (*)(CompanionCallAction, const char*, const char*, void*);
using CompanionMediaCallback = void (*)(bool, const char*, const char*, void*);
using CompanionTimeCallback = void (*)(uint32_t, void*);
using CompanionAvailabilityCallback = void (*)(bool, void*);
// Null title/message remove one UID; null app as well clears the provider session.
using CompanionNotificationCallback = void (*)(const char*, const char*, const char*, uint32_t, bool, void*);

/** Runtime feature support, which can vary by connected phone/session. */
struct CompanionCapabilities {
    bool notifications{false};
    bool media{false};
    bool calls{false};
    bool answerReject{false};
    bool hangup{false};
    bool dial{false};
    bool remoteDismiss{false};
    bool timeSync{false};
};

/**
 * Semantic companion data and command contract independent of radio transport.
 *
 * BluetoothManager consumes this contract to normalize provider events and
 * check capabilities before issuing commands. Source identity remains stable
 * while availability and capabilities may change as peers connect or drop.
 */
class ICompanionSource {
public:
    /** Permit provider cleanup through the interface. */
    virtual ~ICompanionSource() = default;

    // Stable, non-null, machine-readable ID (e.g. "apple-ancs-ams",
    // "android-companion", "linux-mpris"). Keep it independent of transport
    // and UI labels; returned storage must remain valid for the source lifetime.
    virtual const char* sourceId() const = 0;
    /** Initialize provider state after the transport itself has initialized. */
    virtual Result<void> initSource() { return Result<void>(); }
    // Release source-owned transient resources before memory-intensive work.
    // Providers may stop workers, queues, or protocol sessions without
    // requiring the transport or OTA service to know their implementation.
    /** Release provider-owned resources before OTA or other memory-heavy work. */
    virtual bool pauseForMaintenance() { return true; }
    /** Restore provider activity after maintenance; safe after partial pause. */
    virtual void resumeFromMaintenance() {}
    // Called regularly from the application task; asynchronous sources can
    // leave this empty. It must not busy-wait.
    /** Pump bounded deferred work; asynchronous providers may leave this empty. */
    virtual void tickSource() {}
    /** Stop workers and clear callbacks before provider destruction. */
    virtual void shutdownSource() {}
    // Capabilities may vary by peer/session. Notify when availability flips;
    // consumers query capabilities() again after the callback.
    /** Report whether commands/data are currently usable from the peer. */
    virtual bool isAvailable() const = 0;
    /** Return a by-value snapshot so callers cannot retain mutable provider state. */
    virtual CompanionCapabilities capabilities() const = 0;

    // Passing nullptr unregisters the corresponding sink; sources must never
    // retain a callback after shutdownSource().
    /** Install or clear a sink for call-state transitions. */
    virtual void setCallCallback(CompanionCallCallback, void*) = 0;
    /** Install or clear a sink for media metadata/playback updates. */
    virtual void setMediaCallback(CompanionMediaCallback, void*) = 0;
    /** Install or clear a sink for notification add/update/remove events. */
    virtual void setNotificationCallback(CompanionNotificationCallback, void*) = 0;
    /** Install or clear a sink for a peer-provided current time. */
    virtual void setTimeCallback(CompanionTimeCallback, void*) = 0;
    /** Install or clear a sink for provider availability transitions. */
    virtual void setAvailabilityCallback(CompanionAvailabilityCallback, void*) = 0;

    // Return true only when the provider accepted the command for processing;
    // this does not claim that the remote endpoint executed it.
    /** Queue a request to answer the current incoming call. */
    virtual bool acceptCall() = 0;
    /** Queue a request to reject the current incoming call. */
    virtual bool rejectCall() = 0;
    /** Queue a request to end the current active call. */
    virtual bool hangupCall() = 0;
    /** Request dialing; the number format is provider-defined international text. */
    virtual bool dial(const char* number) = 0;
    /** Send a transport-neutral playback action after capability checks. */
    virtual bool mediaCommand(CompanionMediaAction action) = 0;
    /** Request remote removal of one notification when the peer supports it. */
    virtual bool dismissNotification(uint32_t uid) = 0;
};

} // namespace hal
} // namespace ersa
