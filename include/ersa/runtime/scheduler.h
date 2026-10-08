#pragma once

#include <stdint.h>

namespace ersa::runtime {

/** Inputs that must all permit sleep before the platform lock may be released. */
struct SleepEligibility {
    /** True when GPIO/timer sources are configured to recover the UI task. */
    bool wakeSourcesReady{false};
    /** True while the debug/control transport is attached and must stay alive. */
    bool consoleAttached{false};
    /** True after the product's user-interaction idle threshold has elapsed. */
    bool userIdle{false};
    /** True during a call, portal, or other foreground operation. */
    bool foregroundWorkActive{false};
    /** True while network work owns radio or transport resources. */
    bool networkWorkActive{false};
    /** True while the panel is still completing a physical update. */
    bool displayBusy{false};
    /** True while a feature lease prevents the runtime from sleeping. */
    bool featureLeaseHeld{false};
    /** True while input or app state still has work awaiting dispatch/render. */
    bool pendingWork{false};

    /** Return true only when no runtime owner requires the CPU to stay awake. */
    bool maySleep() const {
        return wakeSourcesReady && !consoleAttached && userIdle &&
               !foregroundWorkActive && !networkWorkActive && !displayBusy &&
               !featureLeaseHeld && !pendingWork;
    }
};

/**
 * Bounded, allocation-free accumulator for the next UI wake delay.
 *
 * The caller seeds the maximum useful wait, then contributes each service's
 * remaining deadline. This keeps deadline selection in one place while the
 * owning services remain responsible for their own timing policy.
 */
class WakeDeadlineSet {
public:
    /** Start with the longest time the caller is willing to remain asleep. */
    explicit WakeDeadlineSet(uint32_t maximumWaitMs) : delayMs_(maximumWaitMs) {}

    /** Keep the earlier of the current wait and a service-provided delay. */
    void includeDelay(uint32_t delayMs) {
        if (delayMs < delayMs_) delayMs_ = delayMs;
    }

    /** Return the earliest contributed wake delay, including zero if due now. */
    uint32_t delayMs() const { return delayMs_; }

private:
    uint32_t delayMs_;
};

/** Result for one main-loop wait decision. */
struct LoopSchedule {
    bool allowAutomaticSleep{false};
    uint32_t waitMs{25};
};

/** Keep sleep eligibility and active/idle loop pacing in one tested policy. */
class RuntimeScheduler {
public:
    /**
     * Choose a platform wait from product blockers and the earliest deadline.
     * GPIO interrupts wake the task from sleep, so a fully idle wait can use
     * its real service deadline. Awake pending work gets a short poll;
     * quiescent awake work uses a relaxed cadence.
     */
    static LoopSchedule plan(const SleepEligibility& eligibility,
                             bool appDirty, bool inputPending,
                             uint32_t earliestDeadlineMs) {
        if (eligibility.maySleep()) {
            return {true, earliestDeadlineMs};
        }
        return {false, (appDirty || inputPending) ? 1U : 25U};
    }
};

} // namespace ersa::runtime
