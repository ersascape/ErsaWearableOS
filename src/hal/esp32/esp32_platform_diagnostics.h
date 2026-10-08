#pragma once

#if defined(ARDUINO)
#include "ersa/hal/platform_diagnostics.h"

namespace ersa::hal {

/** ESP-IDF reset, heap, and CPU observations exposed through a generic HAL. */
class Esp32PlatformDiagnostics final : public IPlatformDiagnostics {
public:
    uint32_t resetReasonCode() const override;
    const char* resetReasonName(uint32_t code) const override;
    size_t freeHeapBytes() const override;
    size_t largestFreeHeapBlockBytes() const override;
    uint32_t cpuFrequencyMHz() const override;
};

} // namespace ersa::hal
#endif
