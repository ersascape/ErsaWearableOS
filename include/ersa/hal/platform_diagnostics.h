#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ersa::hal {

/** Platform readings used for diagnostics and resource-aware policies. */
class IPlatformDiagnostics {
public:
    virtual ~IPlatformDiagnostics() = default;
    /** Return the platform reset reason's stable numeric representation. */
    virtual uint32_t resetReasonCode() const = 0;
    /** Convert a reset reason code into a static, human-readable name. */
    virtual const char* resetReasonName(uint32_t code) const = 0;
    /** Return total currently free general-purpose heap in bytes. */
    virtual size_t freeHeapBytes() const = 0;
    /** Return the largest currently allocatable general-purpose block. */
    virtual size_t largestFreeHeapBlockBytes() const = 0;
    /** Return the current CPU clock frequency in megahertz. */
    virtual uint32_t cpuFrequencyMHz() const = 0;
};

} // namespace ersa::hal
