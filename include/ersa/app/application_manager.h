#pragma once

#include "application.h"
#include "ersa/events/event.h"
#include "ersa/hal/display.h"
#include <stdint.h>
#include <stddef.h>

namespace ersa {
namespace app {

/**
 * Owns the fixed-capacity registry and foreground state for firmware apps.
 *
 * Apps are registered as static C++ objects at startup. The manager does not
 * load binaries or allocate app packages at runtime; a bounded registry keeps
 * memory use predictable on the microcontroller. It centralizes lifecycle and
 * dirty-screen state so individual apps do not manage panel refresh policy.
 */
class ApplicationManager {
public:
    /** Maximum number of statically registered apps supported by this build. */
    static constexpr size_t MAX_APPS = 16;

    /** Construct an empty registry with no active application. */
    ApplicationManager();

    /**
     * Add an application to the registry without taking ownership. Static
     * registration avoids heap use and makes the maximum application count a
     * fixed memory budget on the wearable target.
     * @param app Non-owning pointer to a long-lived application instance.
     * @return True if registered; false for null, duplicate, or full registry.
     */
    bool registerApp(Application* app);
    /**
     * Navigate by stable app ID. The old and new apps receive lifecycle
     * callbacks, and a full redraw is scheduled because the screen contents
     * have changed completely.
     * @return False when the ID is null or unknown. Selecting the current app
     *         succeeds without repeating lifecycle callbacks.
     */
    bool switchTo(const char* appId);
    /**
     * Navigate by registry/drawer index; invalid indices leave state unchanged.
     * Selecting the already active index succeeds without another transition.
     */
    bool switchTo(size_t index);

    /** Return the foreground app, or null before an app has been selected. */
    Application* getActiveApp() const;
    /** Return the number of successfully registered apps. */
    size_t getAppCount() const;
    /** Return a non-owning app pointer, or null when the index is out of range. */
    Application* getApp(size_t index) const;

    /**
     * Deliver one event to the foreground app. Inactive screens are excluded
     * so they cannot act on foreground buttons; app adapters request redraws
     * separately through markDirty().
     * @return True when an active app received the event; false when none exists.
     */
    bool handleEvent(const events::Event& event);

    /** Schedule a render; `fullRefresh` also requests a full panel update. */
    void markDirty(bool fullRefresh = false);
    /** Clear pending render flags after the display has been updated. */
    void clearDirty();
    /** Report whether a render is pending. */
    bool isDirty() const;
    /** Report whether the next render must refresh the whole panel. */
    bool isFullRefreshNeeded() const;
    /** Report whether navigation changed the foreground app since last clear. */
    bool isAppSwitched() const;
    /** Clear the app-switch notification after navigation effects are handled. */
    void clearAppSwitched();

    /**
     * Render the active app when dirty and clear the handled refresh state.
     * The manager passes full-refresh intent and app bounds to the display
     * contract. DisplayManager still owns physical refresh cadence and panel
     * power policy, which keeps application code independent from panel limits.
     */
    void render(hal::IDisplay& display);

    /** Run the foreground app's bounded periodic callback. */
    void tick();

    /** Return the firmware's process-wide application registry. */
    static ApplicationManager& instance();

private:
    Application* apps_[MAX_APPS];
    size_t appCount_{0};
    Application* activeApp_{nullptr};

    bool dirty_{true};
    bool fullRefreshNeeded_{true};
    bool appSwitched_{true};
    uint32_t lastRenderTime_{0};
};

} // namespace app
} // namespace ersa
