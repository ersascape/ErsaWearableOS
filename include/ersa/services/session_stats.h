#pragma once

#include <stdint.h>

namespace ersa { namespace services {

class SessionStats {
public:
    /// Load prior-session metadata and begin tracking this boot session.
    static void begin();
    /// Persist session uptime when its reporting interval has elapsed.
    static void tick(uint32_t uptimeMs);
    /// Return the uptime recorded for the preceding session.
    static uint32_t previousSessionUptimeSeconds();
};

} }
