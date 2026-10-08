#pragma once

#if defined(ARDUINO)

#include "ersa/hal/display.h"
#include <Adafruit_GFX.h>
#include <esp_err.h>
#include <driver/spi_master.h>
#include <stddef.h>
#include <stdint.h>

namespace ersa {
namespace drivers {
namespace display {

/**
 * IDisplay adapter using the Terra SSD1681 panel driver and a 1-bit canvas.
 * The controller protocol comes from T1E firmware V2a (MIT, Ampere Works);
 * this adapter retains the existing MiSans assets and generic UI operations.
 */
class Ssd1681Display final : public hal::IDisplay {
public:
    /** Bind panel and SPI pins supplied by the selected Terra BSP. */
    Ssd1681Display(int cs, int dc, int rst, int busy, int sck, int miso, int mosi);
    /** Release this instance's SPI device and bus resources. */
    ~Ssd1681Display() override;

    /** Configure GPIO, initialize SPI, and reset the panel controller. */
    Result<void> init() override;
    /** Return the panel's logical horizontal pixel count. */
    int16_t width() const override;
    /** Return the panel's logical vertical pixel count. */
    int16_t height() const override;
    /** Fill the in-memory canvas without starting a panel update. */
    void clear(hal::Color color = hal::Color::Black) override;
    /** Set one clipped pixel in the in-memory canvas. */
    void drawPixel(int16_t x, int16_t y, hal::Color color) override;
    /** Rasterize a line into the in-memory canvas. */
    void drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, hal::Color color) override;
    /** Fill a clipped rectangle in the in-memory canvas. */
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, hal::Color color) override;
    /** Draw a horizontal line into the in-memory canvas. */
    void drawFastHLine(int16_t x, int16_t y, int16_t w, hal::Color color) override;
    /** Draw a vertical line into the in-memory canvas. */
    void drawFastVLine(int16_t x, int16_t y, int16_t h, hal::Color color) override;
    /** Draw a circle outline into the in-memory canvas. */
    void drawCircle(int16_t x, int16_t y, int16_t r, hal::Color color) override;
    /** Fill a circle in the in-memory canvas. */
    void fillCircle(int16_t x, int16_t y, int16_t r, hal::Color color) override;
    /** Draw a rounded rectangle outline into the in-memory canvas. */
    void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius, hal::Color color) override;
    /** Fill a rounded rectangle in the in-memory canvas. */
    void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius, hal::Color color) override;
    /** Select one existing logical font asset for subsequent text calls. */
    void setFont(hal::FontFace face) override;
    /** Set integer text scale for the built-in font and vector font rasterizer. */
    void setTextSize(uint8_t size) override;
    /** Select black or white for subsequent text rasterization. */
    void setTextColor(hal::Color color) override;
    /** Enable or disable wrapping at the canvas edge. */
    void setTextWrap(bool wrap) override;
    /** Set the text baseline origin for subsequent print calls. */
    void setCursor(int16_t x, int16_t y) override;
    /** Rasterize a null-terminated string at the current cursor. */
    size_t print(const char* text) override;
    /** Rasterize an unsigned byte value at the current cursor. */
    size_t print(uint8_t value) override;
    /** Rasterize a signed integer value at the current cursor. */
    size_t print(int32_t value) override;
    /** Rasterize an unsigned integer value at the current cursor. */
    size_t print(uint32_t value) override;
    /** Measure text using the currently selected font and scale. */
    void getTextBounds(const char* text, int16_t x, int16_t y,
                       int16_t* x1, int16_t* y1, uint16_t* w, uint16_t* h) override;
    /** Send the canvas using a full or differential panel waveform. */
    void refresh(bool full = false) override;
    /** Refresh a clipped, byte-aligned panel window from the canvas. */
    void refreshRect(const Rect& rect) override;
    /** Return the physical panel BUSY input level. */
    bool isBusy() const override;
    /** Register work to service while waiting for the panel BUSY signal. */
    void setBusyCallback(hal::DisplayBusyCallback cb, void* userData = nullptr) override;
    /** Disable the panel's drive rails while retaining the visible image. */
    void powerOff() override;
    /** Mark the driver ready; the panel rails are enabled by the next refresh. */
    void powerOn() override;
    /** Return the driver's logical powered state. */
    bool isPowered() const override;
    /** Return a read-only view of the internal 1-bit canvas. */
    const uint8_t* getBuffer() const override;
    /** Return a mutable view of the internal 1-bit canvas. */
    uint8_t* getBuffer() override;

private:
    static constexpr int16_t kWidth = 200;
    static constexpr int16_t kHeight = 200;
    static constexpr size_t kBufferSize = kWidth * kHeight / 8;

    /** Adafruit_GFX rasterizer backed by the SSD1681's native 1-bpp ordering. */
    class Canvas final : public Adafruit_GFX {
    public:
        explicit Canvas(uint8_t* buffer);
        void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    private:
        uint8_t* buffer_;
    };

    int cs_, dc_, rst_, busy_, sck_, miso_, mosi_;
    spi_device_handle_t spiDevice_{nullptr};
    bool spiBusInitialized_{false};
    bool initialized_{false};
    bool powered_{false};
    bool baselineValid_{false};
    esp_err_t lastIoError_{ESP_OK};
    uint8_t framebuffer_[kBufferSize]{};
    Canvas canvas_;
    hal::DisplayBusyCallback busyCallback_{nullptr};
    void* busyUserData_{nullptr};

    bool initPanel();
    bool refreshFull();
    bool refreshWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    bool writeFull(uint8_t ramCommand);
    bool writeData(const uint8_t* data, size_t length);
    bool writeCommand(uint8_t command);
    bool writeCommandData(uint8_t command, const uint8_t* data, size_t length);
    bool writeCommandByte(uint8_t command, uint8_t data);
    bool setRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    bool waitBusy(uint32_t timeoutMs = 5000);
    bool powerOffPanel();
    void reportIoError(const char* operation);
};

} // namespace display
} // namespace drivers
} // namespace ersa

#endif // ARDUINO
