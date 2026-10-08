#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ersa::hal {

/** Non-blocking byte console used for diagnostics and the host control bridge. */
class IConsole {
public:
    virtual ~IConsole() = default;
    /** Configure the selected platform console before emitting logs. */
    virtual void begin(uint32_t baudRate) = 0;
    /** Report whether a host is attached to the console transport. */
    virtual bool isAttached() const = 0;
    /** Return the amount of output accepted without blocking. */
    virtual size_t availableForWrite() const = 0;
    /** Return the number of input bytes ready to consume. */
    virtual size_t available() const = 0;
    /** Read one byte or return a negative value if none is available. */
    virtual int read() = 0;
    /** Write as many bytes as the platform can accept immediately. */
    virtual size_t write(const uint8_t* data, size_t length) = 0;
    /** Flush pending output when the caller explicitly requests it. */
    virtual void flush() = 0;
};

} // namespace ersa::hal
