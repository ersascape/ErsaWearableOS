#pragma once

#include <stdint.h>
#include <stddef.h>

#if __cplusplus >= 201703L
#include <string_view>
#endif

namespace ersa {
namespace ui {

/** Logical monochrome color vocabulary used by app drawing code. */
enum class Color : uint8_t {
    Black = 0,
    White = 1,
    Inverse = 2
};

/**
 * Small renderer contract for canvas-oriented watchfaces.
 *
 * Canvas is intentionally narrower than a vendor graphics library: it exposes
 * logical pixels, primitive geometry, and text while hiding framebuffer and
 * panel details. It is useful for portable watchface code; the fuller display
 * HAL is used when an app needs font selection or refresh-specific behavior.
 */
class Canvas {
public:
    /** Allow concrete canvases to be destroyed through this interface. */
    virtual ~Canvas() = default;

    /** Return logical width in pixels for layout calculations. */
    virtual int width() const = 0;
    /** Return logical height in pixels for layout calculations. */
    virtual int height() const = 0;

    /** Fill the entire canvas; no hardware refresh is implied. */
    virtual void clear(Color color = Color::Black) = 0;
    /** Set one logical pixel, clipping or ignoring coordinates outside bounds. */
    virtual void drawPixel(int x, int y, Color color = Color::White) = 0;
    /** Rasterize a line between logical endpoints. */
    virtual void drawLine(int x1, int y1, int x2, int y2, Color color = Color::White) = 0;
    /** Draw a rectangle outline with the supplied origin and extent. */
    virtual void drawRect(int x, int y, int w, int h, Color color = Color::White) = 0;
    /** Fill a rectangle in the framebuffer. */
    virtual void fillRect(int x, int y, int w, int h, Color color = Color::White) = 0;
    /** Draw text at the backend-defined baseline convention. */
    virtual void drawText(int x, int y, const char* text, Color color = Color::White) = 0;

#if __cplusplus >= 201703L
    /**
     * Draw a non-null-terminated string_view using a bounded temporary copy.
     * The current embedded implementation limits text to 127 bytes to avoid
     * dynamic allocation and stack growth from untrusted long strings.
     */
    virtual void drawText(int x, int y, std::string_view text, Color color = Color::White) {
        // string_view may not be null-terminated; draw up to length
        char buf[128];
        size_t len = text.length() < sizeof(buf) - 1 ? text.length() : sizeof(buf) - 1;
        for (size_t i = 0; i < len; ++i) buf[i] = text[i];
        buf[len] = '\0';
        drawText(x, y, buf, color);
    }
#endif
};

} // namespace ui
} // namespace ersa
