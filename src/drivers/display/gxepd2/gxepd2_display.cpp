#if defined(ARDUINO)

#include "drivers/display/gxepd2/gxepd2_display.h"
#include "fonts/misans_fonts.h"
#include <stdio.h>
#include <SPI.h>

namespace ersa {
namespace drivers {
namespace display {

GxEpd2Display* GxEpd2Display::s_instance = nullptr;

GxEpd2Display::GxEpd2Display(int cs, int dc, int rst, int busy, int sck, int miso, int mosi)
    : display_(GxEPD2_154_GDEY0154D67(cs, dc, rst, busy)),
      cs_(cs), dc_(dc), rst_(rst), busy_(busy), sck_(sck), miso_(miso), mosi_(mosi) {
    s_instance = this;
}

void GxEpd2Display::staticBusyCallback(const void* p) {
    (void)p;
    // The BSP can service time-sensitive work while this driver waits for a
    // panel waveform, without coupling this peripheral driver to app input.
    if (s_instance && s_instance->busyCb_) {
        s_instance->busyCb_(s_instance->busyUserData_);
    }
    delay(1);
}

Result<void> GxEpd2Display::init() {
    display_.epd2.selectSPI(SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
    display_.init(0, true, 10, false);
    // GxEPD2 calls SPI.begin() without pin arguments from init(). Restore the
    // Terra routing after that call; ESP32-C3 has no default MISO mapping.
    SPI.begin(sck_, miso_, mosi_, cs_);
    display_.epd2.setBusyCallback(staticBusyCallback);
    display_.setRotation(0);
    powered_ = true;
    return Result<void>();
}

int16_t GxEpd2Display::width() const {
    return display_.width();
}

int16_t GxEpd2Display::height() const {
    return display_.height();
}

void GxEpd2Display::clear(Color color) {
    uint16_t c = (color == Color::White) ? GxEPD_WHITE : GxEPD_BLACK;
    display_.fillScreen(c);
}

void GxEpd2Display::drawPixel(int16_t x, int16_t y, Color color) {
    uint16_t c = (color == Color::White) ? GxEPD_WHITE : GxEPD_BLACK;
    display_.drawPixel(x, y, c);
}

void GxEpd2Display::drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, Color color) {
    display_.drawLine(x1, y1, x2, y2, color == Color::White ? GxEPD_WHITE : GxEPD_BLACK);
}

void GxEpd2Display::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Color color) {
    uint16_t c = (color == Color::White) ? GxEPD_WHITE : GxEPD_BLACK;
    display_.fillRect(x, y, w, h, c);
}

void GxEpd2Display::drawFastHLine(int16_t x, int16_t y, int16_t w, Color color) {
    display_.drawFastHLine(x, y, w, color == Color::White ? GxEPD_WHITE : GxEPD_BLACK);
}
void GxEpd2Display::drawFastVLine(int16_t x, int16_t y, int16_t h, Color color) {
    display_.drawFastVLine(x, y, h, color == Color::White ? GxEPD_WHITE : GxEPD_BLACK);
}
void GxEpd2Display::drawCircle(int16_t x, int16_t y, int16_t r, Color color) {
    display_.drawCircle(x, y, r, color == Color::White ? GxEPD_WHITE : GxEPD_BLACK);
}
void GxEpd2Display::fillCircle(int16_t x, int16_t y, int16_t r, Color color) {
    display_.fillCircle(x, y, r, color == Color::White ? GxEPD_WHITE : GxEPD_BLACK);
}
void GxEpd2Display::drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius, Color color) {
    display_.drawRoundRect(x, y, w, h, radius, color == Color::White ? GxEPD_WHITE : GxEPD_BLACK);
}
void GxEpd2Display::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius, Color color) {
    display_.fillRoundRect(x, y, w, h, radius, color == Color::White ? GxEPD_WHITE : GxEPD_BLACK);
}
void GxEpd2Display::setFont(FontFace face) {
    switch (face) {
        case FontFace::Default: display_.setFont(nullptr); break;
        case FontFace::MiSansRegular8: display_.setFont(&MiSansLatin_Regular8pt7b); break;
        case FontFace::MiSansBold8: display_.setFont(&MiSansLatin_Bold8pt7b); break;
        case FontFace::MiSansRegular10: display_.setFont(&MiSansLatin_Regular10pt7b); break;
        case FontFace::MiSansBold10: display_.setFont(&MiSansLatin_Bold10pt7b); break;
        case FontFace::MiSansBold17: display_.setFont(&MiSansLatin_Bold17pt7b); break;
        case FontFace::MiSansLight17: display_.setFont(&MiSansLatin_Light17pt7b); break;
    }
}
void GxEpd2Display::setTextSize(uint8_t size) { display_.setTextSize(size); }
void GxEpd2Display::setTextColor(Color color) { display_.setTextColor(color == Color::White ? GxEPD_WHITE : GxEPD_BLACK); }
void GxEpd2Display::setTextWrap(bool wrap) { display_.setTextWrap(wrap); }
void GxEpd2Display::setCursor(int16_t x, int16_t y) { display_.setCursor(x, y); }
size_t GxEpd2Display::print(const char* text) { return display_.print(text ? text : ""); }
size_t GxEpd2Display::print(uint8_t value) { return display_.print(static_cast<unsigned int>(value)); }
size_t GxEpd2Display::print(int32_t value) { return display_.print(static_cast<long>(value)); }
size_t GxEpd2Display::print(uint32_t value) { return display_.print(static_cast<unsigned long>(value)); }
void GxEpd2Display::getTextBounds(const char* text, int16_t x, int16_t y,
                                  int16_t* x1, int16_t* y1, uint16_t* w, uint16_t* h) {
    display_.getTextBounds(text ? text : "", x, y, x1, y1, w, h);
}

void GxEpd2Display::refresh(bool full) {
    if (full) {
        display_.setFullWindow();
        display_.display(false); // Full refresh with clearing waveform
    } else {
        display_.display(true);  // Fast differential partial refresh
        display_.powerOff();     // Release panel driving voltage; preserve the image
    }
    powered_ = false;
}

void GxEpd2Display::refreshRect(const Rect& rect) {
    const int16_t x = rect.x < 0 ? 0 : rect.x;
    const int16_t y = rect.y < 0 ? 0 : rect.y;
    const int16_t right = (rect.x + rect.w > width()) ? width() : rect.x + rect.w;
    const int16_t bottom = (rect.y + rect.h > height()) ? height() : rect.y + rect.h;
    if (right <= x || bottom <= y) return;
    display_.setFullWindow();
    display_.displayWindow(uint16_t(x), uint16_t(y), uint16_t(right - x), uint16_t(bottom - y));
    // GxEPD2 leaves the panel drive supply on after partial updates. Shut it
    // down so the image does not fade while the MCU is idle.
    display_.powerOff();
    powered_ = false;
}

bool GxEpd2Display::isBusy() const {
    return digitalRead(busy_) == HIGH;
}

void GxEpd2Display::setBusyCallback(DisplayBusyCallback cb, void* userData) {
    busyCb_ = cb;
    busyUserData_ = userData;
}

void GxEpd2Display::powerOff() {
    display_.powerOff();
    powered_ = false;
}

void GxEpd2Display::powerOn() {
    if (powered_) return;
    // powerOff() only disables the panel's drive voltage; the controller and
    // its RAM remain initialized. Resetting it here can strand BUSY and loses
    // the controller's partial-update state. The next GxEPD2 refresh powers
    // the panel as part of its update sequence.
    SPI.begin(sck_, miso_, mosi_, cs_);
    powered_ = true;
}

bool GxEpd2Display::isPowered() const {
    return powered_;
}

const uint8_t* GxEpd2Display::getBuffer() const {
    return nullptr;
}

uint8_t* GxEpd2Display::getBuffer() {
    return nullptr;
}


} // namespace display
} // namespace drivers
} // namespace ersa

#endif // ARDUINO
