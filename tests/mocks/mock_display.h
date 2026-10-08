#pragma once

#include "ersa/hal/display.h"
#include <string>
#include <vector>

namespace ersa {
namespace test {

class MockDisplay : public hal::IDisplay {
public:
    static constexpr int16_t W = 200;
    static constexpr int16_t H = 200;

    MockDisplay() : buffer_(W * H, 0) {}

    Result<void> init() override {
        powered_ = true;
        return Result<void>();
    }

    int16_t width() const override { return W; }
    int16_t height() const override { return H; }

    void clear(hal::Color color = hal::Color::Black) override {
        std::fill(buffer_.begin(), buffer_.end(), (color == hal::Color::White) ? 1 : 0);
    }

    void drawPixel(int16_t x, int16_t y, hal::Color color) override {
        if (x >= 0 && x < W && y >= 0 && y < H) {
            buffer_[y * W + x] = (color == hal::Color::White) ? 1 : 0;
        }
    }

    void drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, hal::Color color) override {
        (void)x1; (void)y1; (void)x2; (void)y2; (void)color;
        drawLineCalls_++;
    }

    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, hal::Color color) override {
        for (int16_t j = y; j < y + h; ++j) {
            for (int16_t i = x; i < x + w; ++i) {
                drawPixel(i, j, color);
            }
        }
    }

    void drawFastHLine(int16_t x, int16_t y, int16_t w, hal::Color color) override { (void)x; (void)y; (void)w; (void)color; }
    void drawFastVLine(int16_t x, int16_t y, int16_t h, hal::Color color) override { (void)x; (void)y; (void)h; (void)color; }
    void drawCircle(int16_t x, int16_t y, int16_t r, hal::Color color) override { (void)x; (void)y; (void)r; (void)color; }
    void fillCircle(int16_t x, int16_t y, int16_t r, hal::Color color) override { (void)x; (void)y; (void)r; (void)color; }
    void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, hal::Color color) override { (void)x; (void)y; (void)w; (void)h; (void)r; (void)color; }
    void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, hal::Color color) override { (void)x; (void)y; (void)w; (void)h; (void)r; (void)color; }
    void setFont(hal::FontFace face) override { font_ = face; }
    void setTextSize(uint8_t size) override { textSize_ = size; }
    void setTextColor(hal::Color color) override { textColor_ = color; }
    void setTextWrap(bool wrap) override { textWrap_ = wrap; }
    void setCursor(int16_t x, int16_t y) override { cursorX_ = x; cursorY_ = y; }
    size_t print(const char* text) override { lastText_ = text ? text : ""; return lastText_.size(); }
    size_t print(uint8_t value) override { lastNumber_ = value; return 1; }
    size_t print(int32_t value) override { lastNumber_ = value; return 1; }
    size_t print(uint32_t value) override { lastNumber_ = static_cast<int32_t>(value); return 1; }
    void getTextBounds(const char* text, int16_t x, int16_t y,
                       int16_t* x1, int16_t* y1, uint16_t* w, uint16_t* h) override {
        (void)x; (void)y;
        if (x1) *x1 = 0;
        if (y1) *y1 = -8;
        if (w) *w = static_cast<uint16_t>(text ? std::char_traits<char>::length(text) * 6 : 0);
        if (h) *h = 8;
    }

    void refresh(bool full = false) override {
        if (full) fullRefreshes_++;
        else partialRefreshes_++;
        powered_ = true;
    }

    void refreshRect(const Rect& rect) override {
        (void)rect;
        partialRefreshes_++;
        powered_ = true;
    }

    bool isBusy() const override { return false; }
    void setBusyCallback(hal::DisplayBusyCallback cb, void* userData = nullptr) override {
        (void)cb; (void)userData;
    }

    void powerOff() override { powered_ = false; }
    void powerOn() override { powered_ = true; }
    bool isPowered() const override { return powered_; }

    const uint8_t* getBuffer() const override { return buffer_.data(); }
    uint8_t* getBuffer() override { return buffer_.data(); }

    hal::Color getPixel(int16_t x, int16_t y) const {
        if (x >= 0 && x < W && y >= 0 && y < H) {
            return buffer_[y * W + x] ? hal::Color::White : hal::Color::Black;
        }
        return hal::Color::Black;
    }

    uint32_t fullRefreshes_{0};
    uint32_t partialRefreshes_{0};
    bool powered_{true};
    uint32_t drawLineCalls_{0};
    hal::FontFace font_{hal::FontFace::MiSansRegular8};
    hal::Color textColor_{hal::Color::White};
    uint8_t textSize_{1};
    bool textWrap_{true};
    int16_t cursorX_{0}, cursorY_{0};
    std::string lastText_;
    int32_t lastNumber_{0};

private:
    std::vector<uint8_t> buffer_;
};

} // namespace test
} // namespace ersa
