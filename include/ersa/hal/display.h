#pragma once

#include "ersa/common/types.h"
#include <stdint.h>
#include <stddef.h>

namespace ersa {
namespace hal {

enum class Color : uint8_t {
    Black = 0,
    White = 1,
    Inverse = 2
};

using DisplayBusyCallback = void (*)(void* userData);

// Logical font assets selected by the application. A concrete display driver
// maps these names to its rendering backend without exposing that backend.
enum class FontFace : uint8_t {
    Default,
    MiSansRegular8,
    MiSansBold8,
    MiSansRegular10,
    MiSansBold10,
    MiSansBold17,
    MiSansLight17
};

class IDisplay {
public:
    virtual ~IDisplay() = default;

    virtual Result<void> init() = 0;
    virtual int16_t width() const = 0;
    virtual int16_t height() const = 0;

    // Buffer manipulation
    virtual void clear(Color color = Color::Black) = 0;
    virtual void drawPixel(int16_t x, int16_t y, Color color) = 0;
    virtual void drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, Color color) = 0;
    virtual void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Color color) = 0;
    virtual void drawFastHLine(int16_t x, int16_t y, int16_t w, Color color) = 0;
    virtual void drawFastVLine(int16_t x, int16_t y, int16_t h, Color color) = 0;
    virtual void drawCircle(int16_t x, int16_t y, int16_t r, Color color) = 0;
    virtual void fillCircle(int16_t x, int16_t y, int16_t r, Color color) = 0;
    virtual void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                               int16_t radius, Color color) = 0;
    virtual void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                               int16_t radius, Color color) = 0;

    // Text operations use logical fonts and colors. They deliberately avoid
    // backend font pointers and graphics-library types.
    virtual void setFont(FontFace face) = 0;
    virtual void setTextSize(uint8_t size) = 0;
    virtual void setTextColor(Color color) = 0;
    virtual void setTextWrap(bool wrap) = 0;
    virtual void setCursor(int16_t x, int16_t y) = 0;
    virtual size_t print(const char* text) = 0;
    virtual size_t print(uint8_t value) = 0;
    virtual size_t print(int32_t value) = 0;
    virtual size_t print(uint32_t value) = 0;
    virtual void getTextBounds(const char* text, int16_t x, int16_t y,
                               int16_t* x1, int16_t* y1,
                               uint16_t* w, uint16_t* h) = 0;

    // Compatibility conveniences for monochrome UI code. These preserve the
    // historical 0=black/1=white convention without leaking driver constants.
    void fillScreen(uint16_t color) { clear(color ? Color::White : Color::Black); }
    void setTextColor(uint16_t color) { setTextColor(color ? Color::White : Color::Black); }
    void drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
        drawLine(x1, y1, x2, y2, color ? Color::White : Color::Black);
    }
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
        drawFastHLine(x, y, w, color ? Color::White : Color::Black);
    }
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
        drawFastVLine(x, y, h, color ? Color::White : Color::Black);
    }
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        fillRect(x, y, w, h, color ? Color::White : Color::Black);
    }
    void drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
        drawCircle(x, y, r, color ? Color::White : Color::Black);
    }
    void fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
        fillCircle(x, y, r, color ? Color::White : Color::Black);
    }
    void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
        drawRoundRect(x, y, w, h, r, color ? Color::White : Color::Black);
    }
    void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
        fillRoundRect(x, y, w, h, r, color ? Color::White : Color::Black);
    }

    // Refresh control
    virtual void refresh(bool full = false) = 0;
    virtual void refreshRect(const Rect& rect) = 0;
    virtual bool isBusy() const = 0;
    virtual void setBusyCallback(DisplayBusyCallback cb, void* userData = nullptr) = 0;

    // Power management
    virtual void powerOff() = 0;
    virtual void powerOn() = 0;
    virtual bool isPowered() const = 0;

    // Direct framebuffer access if available
    virtual const uint8_t* getBuffer() const = 0;
    virtual uint8_t* getBuffer() = 0;
};

} // namespace hal
} // namespace ersa
