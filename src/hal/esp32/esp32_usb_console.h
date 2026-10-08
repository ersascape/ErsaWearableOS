#pragma once

#if defined(ARDUINO)

#include "ersa/hal/console.h"

namespace ersa::hal {

/** USB Serial/JTAG transport; never maps diagnostics onto the display's UART pins. */
class Esp32UsbConsole final : public IConsole {
public:
    void begin(uint32_t baudRate) override;
    bool isAttached() const override;
    size_t availableForWrite() const override;
    size_t available() const override;
    int read() override;
    size_t write(const uint8_t* data, size_t length) override;
    void flush() override;
};

} // namespace ersa::hal

#endif // ARDUINO
