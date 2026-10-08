#pragma once

#if defined(ARDUINO)

#include "ersa/hal/display.h"
#include <GxEPD2_BW.h>
#include <Adafruit_GFX.h>

namespace ersa {
namespace drivers {
namespace display {

using hal::Color;
using hal::DisplayBusyCallback;
using hal::FontFace;
using hal::IDisplay;

class GxEpd2Display : public IDisplay {
public:
    using GxDisplayType = GxEPD2_BW<GxEPD2_154_GDEY0154D67, GxEPD2_154_GDEY0154D67::HEIGHT>;

    GxEpd2Display(int cs, int dc, int rst, int busy, int sck, int miso, int mosi);
    ~GxEpd2Display() override = default;

    Result<void> init() override;
    int16_t width() const override;
    int16_t height() const override;

    void clear(Color color = Color::Black) override;
    void drawPixel(int16_t x, int16_t y, Color color) override;
    void drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, Color color) override;
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Color color) override;
    void drawFastHLine(int16_t x, int16_t y, int16_t w, Color color) override;
    void drawFastVLine(int16_t x, int16_t y, int16_t h, Color color) override;
    void drawCircle(int16_t x, int16_t y, int16_t r, Color color) override;
    void fillCircle(int16_t x, int16_t y, int16_t r, Color color) override;
    void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius, Color color) override;
    void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius, Color color) override;
    void setFont(FontFace face) override;
    void setTextSize(uint8_t size) override;
    void setTextColor(Color color) override;
    void setTextWrap(bool wrap) override;
    void setCursor(int16_t x, int16_t y) override;
    size_t print(const char* text) override;
    size_t print(uint8_t value) override;
    size_t print(int32_t value) override;
    size_t print(uint32_t value) override;
    void getTextBounds(const char* text, int16_t x, int16_t y, int16_t* x1, int16_t* y1, uint16_t* w, uint16_t* h) override;

    void refresh(bool full = false) override;
    void refreshRect(const Rect& rect) override;
    bool isBusy() const override;
    void setBusyCallback(DisplayBusyCallback cb, void* userData = nullptr) override;

    void powerOff() override;
    void powerOn() override;
    bool isPowered() const override;

    const uint8_t* getBuffer() const override;
    uint8_t* getBuffer() override;


private:
    GxDisplayType display_;
    int cs_, dc_, rst_, busy_, sck_, miso_, mosi_;
    DisplayBusyCallback busyCb_{nullptr};
    void* busyUserData_{nullptr};
    bool powered_{false};

    static void staticBusyCallback(const void* p);
    static GxEpd2Display* s_instance;
};

} // namespace display
} // namespace drivers
} // namespace ersa

#endif // ARDUINO
