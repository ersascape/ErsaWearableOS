#pragma once

#include <string>

namespace ersa {
namespace app {

/** Small display-ready value supplied by a watchface complication provider. */
struct ComplicationData {
    /** Text formatted for direct presentation in the watchface layout. */
    std::string text;
};

/**
 * Extension point for watchface data such as weather, battery, or activity.
 * Providers keep data acquisition separate from the watchface renderer so a
 * face can consume a stable value without depending on a particular service.
 */
class ComplicationProvider {
public:
    /** Permit provider implementations to be destroyed through this contract. */
    virtual ~ComplicationProvider() = default;
    /** Return the provider's current presentation value. */
    virtual ComplicationData get() = 0;
};

} // namespace app
} // namespace ersa
