#if defined(ARDUINO)

#include "hal/esp32/esp32_usb_console.h"
#include <Arduino.h>

#if !ARDUINO_USB_CDC_ON_BOOT || !ARDUINO_USB_MODE
#error "Watch logging requires ARDUINO_USB_MODE=1 and ARDUINO_USB_CDC_ON_BOOT=1"
#endif

namespace ersa::hal {

void Esp32UsbConsole::begin(uint32_t baudRate) {
    Serial.setTxBufferSize(512);
    Serial.begin(baudRate);
    Serial.setTxTimeoutMs(0);
}

bool Esp32UsbConsole::isAttached() const { return bool(Serial); }
size_t Esp32UsbConsole::availableForWrite() const {
    const int count = Serial.availableForWrite();
    return count > 0 ? static_cast<size_t>(count) : 0;
}
size_t Esp32UsbConsole::available() const {
    const int count = Serial.available();
    return count > 0 ? static_cast<size_t>(count) : 0;
}
int Esp32UsbConsole::read() { return Serial.read(); }
size_t Esp32UsbConsole::write(const uint8_t* data, size_t length) {
    return data ? Serial.write(data, length) : 0;
}
void Esp32UsbConsole::flush() { Serial.flush(); }

} // namespace ersa::hal

#endif // ARDUINO
