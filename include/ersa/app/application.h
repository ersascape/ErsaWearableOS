#pragma once

#include "ersa/common/types.h"
#include "ersa/events/event.h"
#include "ersa/events/event_bus.h"
#include "ersa/hal/display.h"
#include "ersa/ui/canvas.h"
#include <stdint.h>

namespace ersa {
namespace app {

/**
 * Base contract for a screen or watchface that can be selected by the UI.
 *
 * The manager owns navigation, lifecycle dispatch, refresh scheduling, and
 * event delivery; an application owns only its screen state and behavior.
 * Implementations are statically linked into the firmware, so IDs are stable
 * navigation keys rather than runtime package names. Keep callbacks bounded:
 * long radio or storage operations belong in a service/task, not the UI loop.
 */
class Application : public events::IEventListener {
public:
    /** Destroy through the base interface when application ownership ends. */
    virtual ~Application() = default;

    /** Return a stable, unique, null-terminated navigation key. */
    virtual const char* getId() const = 0;
    /** Return the user-facing name shown by the app drawer. */
    virtual const char* getTitle() const = 0;

    /**
     * Called once before this app is first shown.
     *
     * Establish durable model state here. Do not assume the display is active
     * or block the UI task; the base implementation is empty so simple apps
     * have no setup boilerplate.
     */
    virtual void onCreate() {}
    /**
     * Called when the manager selects this app as the foreground screen.
     * Start foreground-only work, such as app-specific event subscriptions or
     * refresh requests. Pair subscriptions with pause/stop cleanup so hidden
     * screens do not process events unnecessarily.
     */
    virtual void onStart() {}
    /**
     * Called after onPause() when this app is foregrounded again. Revalidate
     * transient state and resume paused work; durable model state normally
     * remains intact across this temporary transition.
     */
    virtual void onResume() {}
    /**
     * Called before another screen temporarily covers this app. Stop
     * animations and polling, and release temporary wake/performance leases.
     * The manager retains the app instance, so this is not destruction.
     */
    virtual void onPause() {}
    /**
     * Called when navigation leaves this app. Release foreground-only
     * resources and callbacks. Use onPause() for a temporary cover and
     * onDestroy() only when the manager permanently removes the object.
     */
    virtual void onStop() {}
    /**
     * Called when the manager permanently removes this app. Free app-owned
     * resources and unregister any remaining listeners. Static firmware apps
     * normally live for an entire boot, so this callback is uncommon.
     */
    virtual void onDestroy() {}

    /**
     * Compatibility hook for adapters using the earlier enter/exit lifecycle.
     * New app code should use onStart() and onResume(); this no-op remains so
     * existing adapters continue to compile.
     */
    virtual void onEnter() {}
    /** Compatibility counterpart of onEnter(); prefer onPause() or onStop(). */
    virtual void onExit() {}

    /**
     * Receive a normalized system or input event while this app is active.
     * Override this to translate button, clock, battery, and companion events
     * into app state changes. Payloads are copied values, so app logic does not
     * need to depend on the device driver that produced the event. The base
     * implementation intentionally ignores events for static screens.
     */
    void onEvent(const events::Event& event) override { (void)event; }

    /**
     * Render through the display HAL. `fullRefresh` requests a panel-wide
     * update when the hardware supports a distinct full-refresh waveform.
     * The UI calls this only when content is dirty. Draw into the framebuffer;
     * the display manager owns physical refresh timing and panel power so app
     * code stays independent of e-paper driver APIs.
     */
    virtual void render(hal::IDisplay& display, bool fullRefresh) { (void)display; (void)fullRefresh; }
    /**
     * Render through the lightweight canvas contract used by watchfaces.
     * This overload supports canvas-oriented apps; override the overload used
     * by the registry adapter for the app being implemented.
     */
    virtual void render(ui::Canvas& canvas) { (void)canvas; }

    /**
     * Return the smallest display rectangle that may have changed. The UI uses
     * this to limit partial e-paper refreshes and reduce visible flashing;
     * return the whole panel when the changed region cannot be bounded. The
     * default describes the 200 by 200 reference canvas and is conservative
     * for the current product; other geometries should provide their own.
     */
    virtual Rect getPartialBounds() const { return Rect{0, 0, 200, 200}; }

    /**
     * Perform short periodic work while this application is active.
     * The manager invokes this from the UI task, so return promptly. Use a
     * service or worker for network, filesystem, or radio operations and let
     * tick() consume their completed results instead of waiting for I/O.
     */
    virtual void tick() {}
};

} // namespace app
} // namespace ersa
