#pragma once

#include "event.h"
#include <stdint.h>
#include <stddef.h>

namespace ersa {
namespace events {

using SubscriptionId = uint16_t;
using EventCallback = void (*)(const Event& event, void* userData);

class IEventListener {
public:
    /** Enable lifetime-safe polymorphic cleanup of event listeners. */
    virtual ~IEventListener() = default;
    /** Consume one normalized event delivered by an EventBus subscription. */
    virtual void onEvent(const Event& event) = 0;
};

/**
 * One bounded EventBus registration. Callback and listener pointers are
 * non-owning: the subscriber must unregister before its target is destroyed.
 */
struct Subscription {
    /** Handle returned to callers and used for unsubscribe(). */
    SubscriptionId id{0};
    /** Event filter; None matches every event type. */
    EventType type{EventType::None}; // None = match all events
    /** Optional C callback used instead of the listener interface. */
    EventCallback callback{nullptr};
    /** Optional object listener used instead of the C callback. */
    IEventListener* listener{nullptr};
    /** Caller context passed back unchanged to `callback`. */
    void* userData{nullptr};
    /** False means the fixed slot is unused and may be recycled. */
    bool active{false};
};

/**
 * Fixed-memory event distribution for services, HAL adapters, and apps.
 *
 * `publish` dispatches synchronously; `post` copies into a bounded queue for
 * later dispatch by the application task. This split lets interrupt/radio
 * callbacks hand off normalized events without running UI code on their
 * thread, while avoiding heap allocation in the event path.
 */
class EventBus {
public:
    /** Maximum simultaneous callback/listener registrations. */
    static constexpr size_t MAX_SUBSCRIBERS = 24;
    /** Maximum queued events awaiting dispatchQueue(). */
    static constexpr size_t MAX_QUEUE = 32;

    /** Construct an empty bus with an empty queue and fresh subscription IDs. */
    EventBus();

    /**
     * Register a C-style callback for an event type.
     * @param type Event filter; `None` receives every event.
     * @param callback Function called synchronously during dispatch.
     * @param userData Opaque context forwarded to the callback.
     * @return Nonzero subscription handle, or zero when no slot is available.
     */
    SubscriptionId subscribe(EventType type, EventCallback callback, void* userData = nullptr);

    /** Register an object listener for an event type; see callback overload. */
    SubscriptionId subscribe(EventType type, IEventListener* listener);

    /** Remove a registration by handle; unknown handles return false. */
    bool unsubscribe(SubscriptionId id);

    /** Immediately call matching subscribers on the caller's thread. */
    void publish(const Event& event);

    /**
     * Copy an event into the ring queue for later application-task delivery.
     * @return False if the bounded queue is full; callers should handle loss.
     */
    bool post(const Event& event);

    /** Dispatch queued events in FIFO order and return the number processed. */
    size_t dispatchQueue();

    /** Clear all subscriptions and queued events, primarily for reinitialization/tests. */
    void clear();

    /** Return the process-wide default bus used by system services. */
    static EventBus& instance();

private:
    Subscription subscribers_[MAX_SUBSCRIBERS];
    SubscriptionId nextSubId_{1};

    Event queue_[MAX_QUEUE];
    size_t queueHead_{0};
    size_t queueTail_{0};
    size_t queueCount_{0};
};

} // namespace events
} // namespace ersa
