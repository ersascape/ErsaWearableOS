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

/**
 * Graphics, text, refresh, and power contract consumed by watch UI code.
 *
 * This is the stable renderer-facing surface for every board. Applications
 * draw using logical colors, fonts, and coordinates; the selected driver maps
 * those operations to e-paper, OLED, or another panel. Keeping vendor drawing
 * types out of this interface lets a different panel driver be substituted
 * without rewriting app code. Drawing mutates a framebuffer; refresh methods
 * are the separate operation that transmits that buffer to the physical panel.
 */
class IDisplay {
public:
    /** Allow a concrete panel driver to be destroyed through this contract. */
    virtual ~IDisplay() = default;

    /**
     * Initialize the panel and its drawing buffer.
     * @return An error when the controller or required buffer is unavailable.
     */
    virtual Result<void> init() = 0;
    /** Return horizontal logical resolution used by app coordinates. */
    virtual int16_t width() const = 0;
    /** Return vertical logical resolution used by app coordinates. */
    virtual int16_t height() const = 0;

    // Buffer manipulation
    /**
     * Fill the drawing buffer with one logical color; this does not itself
     * trigger a physical panel refresh.
     */
    virtual void clear(Color color = Color::Black) = 0;
    /** Set the framebuffer pixel at (`x`, `y`), clipping unsupported coordinates. */
    virtual void drawPixel(int16_t x, int16_t y, Color color) = 0;
    /** Rasterize a line from (`x1`, `y1`) to (`x2`, `y2`) in the framebuffer. */
    virtual void drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, Color color) = 0;
    /** Fill a rectangle with its top-left corner at (`x`, `y`). */
    virtual void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Color color) = 0;
    /** Draw a horizontal line, allowing the backend to optimize row access. */
    virtual void drawFastHLine(int16_t x, int16_t y, int16_t w, Color color) = 0;
    /** Draw a vertical line, allowing the backend to optimize column access. */
    virtual void drawFastVLine(int16_t x, int16_t y, int16_t h, Color color) = 0;
    /** Draw a circle outline centered at (`x`, `y`) with the given radius. */
    virtual void drawCircle(int16_t x, int16_t y, int16_t r, Color color) = 0;
    /** Fill a circle centered at (`x`, `y`) with the given radius. */
    virtual void fillCircle(int16_t x, int16_t y, int16_t r, Color color) = 0;
    /// Draw an unfilled rectangle with rounded corners.
    virtual void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                               int16_t radius, Color color) = 0;
    /// Draw a filled rectangle with rounded corners.
    virtual void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                               int16_t radius, Color color) = 0;

    // Text operations use logical fonts and colors. They deliberately avoid
    // backend font pointers and graphics-library types.
    /** Select an asset-level font name; drivers map it to their own font data. */
    virtual void setFont(FontFace face) = 0;
    /** Set text scaling; size 1 is the backend's normal scale. */
    virtual void setTextSize(uint8_t size) = 0;
    /** Set the color used by subsequent print() calls. */
    virtual void setTextColor(Color color) = 0;
    /** Control whether subsequent text wraps when it reaches the canvas edge. */
    virtual void setTextWrap(bool wrap) = 0;
    /** Set the text baseline origin used by subsequent print() calls. */
    virtual void setCursor(int16_t x, int16_t y) = 0;
    /**
     * Draw a null-terminated string at the current cursor and return its
     * consumed character count. The pointer is borrowed for this call only.
     */
    virtual size_t print(const char* text) = 0;
    /// Draw an unsigned byte value and return the number of characters consumed.
    virtual size_t print(uint8_t value) = 0;
    /// Draw a signed 32-bit value and return the number of characters consumed.
    virtual size_t print(int32_t value) = 0;
    /// Draw an unsigned 32-bit value and return the number of characters consumed.
    virtual size_t print(uint32_t value) = 0;
    /**
     * Measure text using the current font/size without drawing it.
     * @param text Null-terminated string to measure.
     * @param x Baseline origin x coordinate.
     * @param y Baseline origin y coordinate.
     * @param x1 Receives the left edge relative to the supplied origin.
     * @param y1 Receives the top edge relative to the supplied baseline.
     * @param w Receives measured width in pixels.
     * @param h Receives measured height in pixels.
     * Callers should provide non-null output pointers supported by the driver.
     */
    virtual void getTextBounds(const char* text, int16_t x, int16_t y,
                               int16_t* x1, int16_t* y1,
                               uint16_t* w, uint16_t* h) = 0;

    // Compatibility conveniences preserve the historical 0=black/1=white
    // convention for older monochrome UI code. New code should use Color so
    // intent is explicit and no panel-specific numeric constant leaks upward.
    /// Clear the framebuffer using the legacy 0=black, nonzero=white mapping.
    void fillScreen(uint16_t color) { clear(color ? Color::White : Color::Black); }
    /// Set legacy monochrome text color; nonzero maps to white.
    void setTextColor(uint16_t color) { setTextColor(color ? Color::White : Color::Black); }
    /// Draw a line using the legacy monochrome color convention.
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

    // Refresh control is separate from drawing so the UI can coalesce multiple
    // drawing operations into one panel update and honor display timing limits.
    /**
     * Transmit the framebuffer to the panel.
     * @param full Request a full waveform when true; partial waveform otherwise.
     */
    virtual void refresh(bool full = false) = 0;
    /**
     * Update only the specified region when the panel supports partial refresh.
     * Drivers without region updates may safely promote this to a full refresh.
     */
    virtual void refreshRect(const Rect& rect) = 0;
    /// Return whether the panel is still processing a refresh.
    virtual bool isBusy() const = 0;
    /**
     * Register a completion/wait callback for panel busy handling.
     * @param cb Callback invoked by the driver, or null to clear it.
     * @param userData Opaque context passed back unchanged to `cb`.
     */
    virtual void setBusyCallback(DisplayBusyCallback cb, void* userData = nullptr) = 0;

    // Power management
    /** Put the panel controller into low power without discarding framebuffer data. */
    virtual void powerOff() = 0;
    /** Restore panel-controller power before a refresh or status query. */
    virtual void powerOn() = 0;
    /// Return whether the panel controller is currently powered.
    virtual bool isPowered() const = 0;

    // Direct framebuffer access if available
    /** Return a non-owning read-only framebuffer view, or null if unavailable. */
    virtual const uint8_t* getBuffer() const = 0;
    /**
     * Return a non-owning mutable framebuffer view, or null if unavailable.
     * Direct access is an optimization; callers must still schedule refresh().
     */
    virtual uint8_t* getBuffer() = 0;
};

} // namespace hal
} // namespace ersa
