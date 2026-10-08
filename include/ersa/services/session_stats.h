#pragma once

#include <stdint.h>

namespace ersa { namespace services {

/**
 * Tracks uptime across boots using a small persistent session record.
 * The service stores checkpoints rather than writing flash every UI tick, so
 * callers can report the previous session while limiting flash wear.
 */
class SessionStats {
public:
    /** Load prior-session metadata and start the current uptime counter. */
    static void begin();
    /** Persist a checkpoint only when the configured reporting interval elapsed. */
    static void tick(uint32_t uptimeMs);
    /** Return the last persisted uptime, in seconds, from the previous boot. */
    static uint32_t previousSessionUptimeSeconds();
};

} }
