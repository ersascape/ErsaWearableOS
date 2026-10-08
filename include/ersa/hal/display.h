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

/// Graphics, text, refresh, and power contract consumed by watch UI code.
class IDisplay {
public:
    virtual ~IDisplay() = default;

    /// Initialize the panel and framebuffer.
    virtual Result<void> init() = 0;
    /// Return the logical canvas width in pixels.
    virtual int16_t width() const = 0;
    /// Return the logical canvas height in pixels.
    virtual int16_t height() const = 0;

    // Buffer manipulation
    /// Clear the canvas to a logical color.
    virtual void clear(Color color = Color::Black) = 0;
    /// Set one pixel to a logical color.
    virtual void drawPixel(int16_t x, int16_t y, Color color) = 0;
    /// Draw a line between two canvas points.
    virtual void drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, Color color) = 0;
    /// Fill an axis-aligned rectangle.
    virtual void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Color color) = 0;
    /// Draw a horizontal line using the backend's optimized primitive.
    virtual void drawFastHLine(int16_t x, int16_t y, int16_t w, Color color) = 0;
    /// Draw a vertical line using the backend's optimized primitive.
    virtual void drawFastVLine(int16_t x, int16_t y, int16_t h, Color color) = 0;
    /// Draw an unfilled circle.
    virtual void drawCircle(int16_t x, int16_t y, int16_t r, Color color) = 0;
    /// Draw a filled circle.
    virtual void fillCircle(int16_t x, int16_t y, int16_t r, Color color) = 0;
    /// Draw an unfilled rectangle with rounded corners.
    virtual void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                               int16_t radius, Color color) = 0;
    /// Draw a filled rectangle with rounded corners.
    virtual void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                               int16_t radius, Color color) = 0;

    // Text operations use logical fonts and colors. They deliberately avoid
    // backend font pointers and graphics-library types.
    /// Select a logical font face supported by the renderer.
    virtual void setFont(FontFace face) = 0;
    /// Set the text scale factor.
    virtual void setTextSize(uint8_t size) = 0;
    /// Set the foreground text color.
    virtual void setTextColor(Color color) = 0;
    /// Enable or disable wrapping at the canvas edge.
    virtual void setTextWrap(bool wrap) = 0;
    /// Set the baseline cursor for subsequent text output.
    virtual void setCursor(int16_t x, int16_t y) = 0;
    /// Draw a null-terminated string and return the number of characters consumed.
    virtual size_t print(const char* text) = 0;
    /// Draw an unsigned byte value and return the number of characters consumed.
    virtual size_t print(uint8_t value) = 0;
    /// Draw a signed 32-bit value and return the number of characters consumed.
    virtual size_t print(int32_t value) = 0;
    /// Draw an unsigned 32-bit value and return the number of characters consumed.
    virtual size_t print(uint32_t value) = 0;
    /// Measure a string at the requested baseline using the selected font.
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
    /// Refresh the full panel, optionally using the slower full waveform.
    virtual void refresh(bool full = false) = 0;
    /// Refresh the specified canvas rectangle when the backend supports it.
    virtual void refreshRect(const Rect& rect) = 0;
    /// Return whether the panel is still processing a refresh.
    virtual bool isBusy() const = 0;
    /// Register a callback used while waiting for panel completion.
    virtual void setBusyCallback(DisplayBusyCallback cb, void* userData = nullptr) = 0;

    // Power management
    /// Put the panel controller into its low-power state.
    virtual void powerOff() = 0;
    /// Wake or initialize the panel controller.
    virtual void powerOn() = 0;
    /// Return whether the panel controller is currently powered.
    virtual bool isPowered() const = 0;

    // Direct framebuffer access if available
    /// Return a read-only view of the framebuffer when one is available.
    virtual const uint8_t* getBuffer() const = 0;
    /// Return a mutable view of the framebuffer when one is available.
    virtual uint8_t* getBuffer() = 0;
};

} // namespace hal
} // namespace ersa
