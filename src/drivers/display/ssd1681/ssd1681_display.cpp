#if defined(ARDUINO)

#include "drivers/display/ssd1681/ssd1681_display.h"
#include "fonts/misans_fonts.h"
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

namespace ersa {
namespace drivers {
namespace display {
namespace {

constexpr const char* kTag = "SSD1681";
constexpr uint8_t kWidthBytes = 25;
constexpr int64_t kDeghostIntervalUs = 5LL * 60LL * 1000000LL;

// Differential SSD1681 waveform for the Terra GDEY0154D67 panel, ported from
// T1E firmware V2a. The LUT bypasses the controller's OTP partial waveform.
constexpr uint8_t kPartialLut[159] = {
    0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x80,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x40,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x08,0x00,0x00,0x00,0x00,0x00,0x00,
    0x01,0x01,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x22,0x22,0x22,0x22,0x22,0x22,0x00,0x00,0x00,
    0x02,0x17,0x41,0xB0,0x32,0x28
};

} // namespace

Ssd1681Display::Canvas::Canvas(uint8_t* buffer)
    : Adafruit_GFX(kWidth, kHeight), buffer_(buffer) {}

void Ssd1681Display::Canvas::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return;
    const size_t index = size_t(y) * kWidthBytes + size_t(x / 8);
    const uint8_t mask = uint8_t(0x80u >> (x & 7));
    if (color) buffer_[index] |= mask;
    else buffer_[index] &= uint8_t(~mask);
}

Ssd1681Display::Ssd1681Display(int cs, int dc, int rst, int busy, int sck, int miso, int mosi)
    : cs_(cs), dc_(dc), rst_(rst), busy_(busy), sck_(sck), miso_(miso), mosi_(mosi),
      canvas_(framebuffer_) {}

Ssd1681Display::~Ssd1681Display() {
    if (spiDevice_) spi_bus_remove_device(spiDevice_);
    if (spiBusInitialized_) spi_bus_free(SPI2_HOST);
}

Result<void> Ssd1681Display::init() {
    if (initialized_) return Result<void>();

    gpio_config_t outputs{};
    outputs.pin_bit_mask = (1ULL << cs_) | (1ULL << dc_) | (1ULL << rst_);
    outputs.mode = GPIO_MODE_OUTPUT;
    outputs.pull_up_en = GPIO_PULLUP_DISABLE;
    outputs.pull_down_en = GPIO_PULLDOWN_DISABLE;
    outputs.intr_type = GPIO_INTR_DISABLE;
    esp_err_t error = gpio_config(&outputs);
    if (error != ESP_OK) return Result<void>(ErrorCode::HardwareFault, esp_err_to_name(error));

    gpio_config_t input{};
    input.pin_bit_mask = 1ULL << busy_;
    input.mode = GPIO_MODE_INPUT;
    input.pull_up_en = GPIO_PULLUP_DISABLE;
    input.pull_down_en = GPIO_PULLDOWN_DISABLE;
    input.intr_type = GPIO_INTR_DISABLE;
    error = gpio_config(&input);
    if (error != ESP_OK) return Result<void>(ErrorCode::HardwareFault, esp_err_to_name(error));

    gpio_set_level(gpio_num_t(cs_), 1);
    gpio_set_level(gpio_num_t(rst_), 1);

    spi_bus_config_t bus{};
    bus.mosi_io_num = mosi_;
    bus.miso_io_num = miso_;
    bus.sclk_io_num = sck_;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = kBufferSize + 64;
    error = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (error != ESP_OK) return Result<void>(ErrorCode::HardwareFault, esp_err_to_name(error));
    spiBusInitialized_ = true;

    spi_device_interface_config_t device{};
    device.clock_speed_hz = 4000000;
    device.mode = 0;
    device.spics_io_num = -1; // D/C and CS are controlled as GPIOs per SSD1681 transaction.
    device.queue_size = 1;
    error = spi_bus_add_device(SPI2_HOST, &device, &spiDevice_);
    if (error != ESP_OK) return Result<void>(ErrorCode::HardwareFault, esp_err_to_name(error));

    if (!initPanel()) {
        reportIoError();
        return Result<void>(ErrorCode::HardwareFault, "SSD1681 initialization failed");
    }
    initialized_ = true;
    powered_ = true;
    return Result<void>();
}

int16_t Ssd1681Display::width() const { return kWidth; }
int16_t Ssd1681Display::height() const { return kHeight; }

void Ssd1681Display::clear(hal::Color color) {
    memset(framebuffer_, color == hal::Color::White ? 0xFF : 0x00, sizeof(framebuffer_));
}

void Ssd1681Display::drawPixel(int16_t x, int16_t y, hal::Color color) {
    canvas_.drawPixel(x, y, color == hal::Color::White ? 1 : 0);
}

void Ssd1681Display::drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, hal::Color color) {
    canvas_.drawLine(x1, y1, x2, y2, color == hal::Color::White ? 1 : 0);
}

void Ssd1681Display::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, hal::Color color) {
    canvas_.fillRect(x, y, w, h, color == hal::Color::White ? 1 : 0);
}

void Ssd1681Display::drawFastHLine(int16_t x, int16_t y, int16_t w, hal::Color color) {
    canvas_.drawFastHLine(x, y, w, color == hal::Color::White ? 1 : 0);
}

void Ssd1681Display::drawFastVLine(int16_t x, int16_t y, int16_t h, hal::Color color) {
    canvas_.drawFastVLine(x, y, h, color == hal::Color::White ? 1 : 0);
}

void Ssd1681Display::drawCircle(int16_t x, int16_t y, int16_t r, hal::Color color) {
    canvas_.drawCircle(x, y, r, color == hal::Color::White ? 1 : 0);
}

void Ssd1681Display::fillCircle(int16_t x, int16_t y, int16_t r, hal::Color color) {
    canvas_.fillCircle(x, y, r, color == hal::Color::White ? 1 : 0);
}

void Ssd1681Display::drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                                   int16_t radius, hal::Color color) {
    canvas_.drawRoundRect(x, y, w, h, radius, color == hal::Color::White ? 1 : 0);
}

void Ssd1681Display::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                                   int16_t radius, hal::Color color) {
    canvas_.fillRoundRect(x, y, w, h, radius, color == hal::Color::White ? 1 : 0);
}

void Ssd1681Display::setFont(hal::FontFace face) {
    switch (face) {
        case hal::FontFace::Default: canvas_.setFont(nullptr); break;
        case hal::FontFace::MiSansRegular8: canvas_.setFont(&MiSansLatin_Regular8pt7b); break;
        case hal::FontFace::MiSansBold8: canvas_.setFont(&MiSansLatin_Bold8pt7b); break;
        case hal::FontFace::MiSansRegular10: canvas_.setFont(&MiSansLatin_Regular10pt7b); break;
        case hal::FontFace::MiSansBold10: canvas_.setFont(&MiSansLatin_Bold10pt7b); break;
        case hal::FontFace::MiSansBold17: canvas_.setFont(&MiSansLatin_Bold17pt7b); break;
        case hal::FontFace::MiSansLight17: canvas_.setFont(&MiSansLatin_Light17pt7b); break;
    }
}

void Ssd1681Display::setTextSize(uint8_t size) { canvas_.setTextSize(size); }
void Ssd1681Display::setTextColor(hal::Color color) { canvas_.setTextColor(color == hal::Color::White ? 1 : 0); }
void Ssd1681Display::setTextWrap(bool wrap) { canvas_.setTextWrap(wrap); }
void Ssd1681Display::setCursor(int16_t x, int16_t y) { canvas_.setCursor(x, y); }
size_t Ssd1681Display::print(const char* text) { return canvas_.print(text ? text : ""); }
size_t Ssd1681Display::print(uint8_t value) { return canvas_.print(static_cast<unsigned int>(value)); }
size_t Ssd1681Display::print(int32_t value) { return canvas_.print(static_cast<long>(value)); }
size_t Ssd1681Display::print(uint32_t value) { return canvas_.print(static_cast<unsigned long>(value)); }

void Ssd1681Display::getTextBounds(const char* text, int16_t x, int16_t y,
                                   int16_t* x1, int16_t* y1, uint16_t* w, uint16_t* h) {
    canvas_.getTextBounds(text ? text : "", x, y, x1, y1, w, h);
}

void Ssd1681Display::refresh(bool full) {
    if (!initialized_) return;
    lastIoError_ = ESP_OK;
    const bool ok = full ? refreshFull() : refreshWindow(0, 0, kWidth, kHeight);
    if (!ok) reportIoError();
    else {
        powered_ = true;
        if (!full && !powerOffPanel()) reportIoError();
        powered_ = false;
    }
}

void Ssd1681Display::refreshRect(const Rect& rect) {
    if (!initialized_ || rect.isEmpty()) return;
    const int32_t left = rect.x < 0 ? 0 : rect.x;
    const int32_t top = rect.y < 0 ? 0 : rect.y;
    const int32_t right = (int32_t(rect.x) + rect.w > kWidth) ? kWidth : int32_t(rect.x) + rect.w;
    const int32_t bottom = (int32_t(rect.y) + rect.h > kHeight) ? kHeight : int32_t(rect.y) + rect.h;
    if (right <= left || bottom <= top) return;

    const uint16_t x = uint16_t(left & ~7);
    const uint16_t alignedRight = uint16_t((right + 7) & ~7);
    const uint16_t w = (alignedRight > kWidth ? kWidth : alignedRight) - x;
    lastIoError_ = ESP_OK;
    if (!refreshWindow(x, uint16_t(top), w, uint16_t(bottom - top))) {
        reportIoError();
        return;
    }
    if (!powerOffPanel()) reportIoError();
    powered_ = false;
}

bool Ssd1681Display::isBusy() const {
    return gpio_get_level(gpio_num_t(busy_)) == 1;
}

void Ssd1681Display::setBusyCallback(hal::DisplayBusyCallback cb, void* userData) {
    busyCallback_ = cb;
    busyUserData_ = userData;
}

void Ssd1681Display::powerOff() {
    if (initialized_ && powered_) {
        lastIoError_ = ESP_OK;
        if (!powerOffPanel()) reportIoError();
    }
    powered_ = false;
}

void Ssd1681Display::powerOn() {
    // The controller's panel-driving rails are enabled by the next refresh.
    powered_ = initialized_;
}

bool Ssd1681Display::isPowered() const { return powered_; }
const uint8_t* Ssd1681Display::getBuffer() const { return framebuffer_; }
uint8_t* Ssd1681Display::getBuffer() { return framebuffer_; }

bool Ssd1681Display::initPanel() {
    gpio_set_level(gpio_num_t(rst_), 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(gpio_num_t(rst_), 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(gpio_num_t(rst_), 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    if (!waitBusy()) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
    if (!writeCommand(0x12)) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
    if (!waitBusy()) return false;
    const uint8_t driverOutput[] = {0xC7, 0x00, 0x00};
    return writeCommandData(0x01, driverOutput, sizeof(driverOutput)) &&
           writeCommandByte(0x3C, 0x05) && writeCommandByte(0x18, 0x80) &&
           setRamArea(0, 0, kWidth, kHeight);
}

bool Ssd1681Display::refreshFull() {
    if (!writeFull(0x24) || !writeFull(0x26) || !writeCommandByte(0x22, 0xF7) ||
        !writeCommand(0x20) || !waitBusy()) return false;
    baselineValid_ = true;
    lastDeghostUs_ = esp_timer_get_time();
    return true;
}

bool Ssd1681Display::refreshWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!baselineValid_) return refreshFull();
    if (!w || !h) return true;
    if (x >= kWidth || y >= kHeight || x + w > kWidth || y + h > kHeight) return false;

    // Partial waveforms leave a small amount of pigment residue. Use the
    // controller's full waveform periodically, even when only a small region
    // changed, so the panel receives a regular ghost-clearing refresh.
    if (esp_timer_get_time() - lastDeghostUs_ >= kDeghostIntervalUs)
        return refreshFull();

    // The first 153 bytes are the waveform table; the remaining bytes program
    // its border and panel voltages. Hardware reset is intentionally avoided
    // here because it would discard the custom partial-refresh LUT.
    if (!writeCommandData(0x32, kPartialLut, 153) ||
        !writeCommandByte(0x3F, kPartialLut[153]) ||
        !writeCommandByte(0x03, kPartialLut[154]) ||
        !writeCommandData(0x04, &kPartialLut[155], 3) ||
        !writeCommandByte(0x2C, kPartialLut[158])) return false;

    const uint8_t displayOption[] = {0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x00};
    if (!writeCommandData(0x37, displayOption, sizeof(displayOption)) ||
        !writeCommandByte(0x3C, 0x80)) return false;

    const uint16_t x0 = x / 8;
    const uint16_t x1 = (x + w - 1) / 8;
    const uint8_t xRange[] = {uint8_t(x0), uint8_t(x1)};
    const uint8_t yRange[] = {uint8_t(y), uint8_t(y >> 8),
                              uint8_t(y + h - 1), uint8_t((y + h - 1) >> 8)};
    if (!writeCommandByte(0x11, 0x03) || !writeCommandData(0x44, xRange, sizeof(xRange)) ||
        !writeCommandData(0x45, yRange, sizeof(yRange)) ||
        !writeCommandByte(0x4E, uint8_t(x0)) ||
        !writeCommandData(0x4F, &yRange[0], 2)) return false;

    if (!writeCommand(0x24)) return false;
    const size_t rowBytes = x1 - x0 + 1;
    for (uint16_t row = y; row < y + h; ++row) {
        const uint8_t* rowData = framebuffer_ + size_t(row) * kWidthBytes + x0;
        if (!writeData(rowData, rowBytes)) return false;
    }

    if (!writeCommandByte(0x22, 0xC7) || !writeCommand(0x20) || !waitBusy()) return false;
    // Keep controller old/current RAM synchronized for the next differential
    // update. The V2a algorithm intentionally writes the full image here.
    return writeFull(0x26) && writeFull(0x24);
}

bool Ssd1681Display::writeFull(uint8_t ramCommand) {
    if (!setRamArea(0, 0, kWidth, kHeight) || !writeCommand(ramCommand)) return false;
    return writeData(framebuffer_, sizeof(framebuffer_));
}

bool Ssd1681Display::writeData(const uint8_t* data, size_t length) {
    if (lastIoError_ != ESP_OK || !spiDevice_) return false;
    gpio_set_level(gpio_num_t(cs_), 0);
    gpio_set_level(gpio_num_t(dc_), 1);
    spi_transaction_t transaction{};
    transaction.length = length * 8;
    transaction.tx_buffer = data;
    lastIoError_ = spi_device_transmit(spiDevice_, &transaction);
    gpio_set_level(gpio_num_t(cs_), 1);
    return lastIoError_ == ESP_OK;
}

bool Ssd1681Display::writeCommand(uint8_t command) {
    if (lastIoError_ != ESP_OK || !spiDevice_) return false;
    gpio_set_level(gpio_num_t(cs_), 0);
    gpio_set_level(gpio_num_t(dc_), 0);
    spi_transaction_t transaction{};
    transaction.length = 8;
    transaction.tx_buffer = &command;
    lastIoError_ = spi_device_transmit(spiDevice_, &transaction);
    gpio_set_level(gpio_num_t(cs_), 1);
    return lastIoError_ == ESP_OK;
}

bool Ssd1681Display::writeCommandData(uint8_t command, const uint8_t* data, size_t length) {
    if (lastIoError_ != ESP_OK || !spiDevice_) return false;
    gpio_set_level(gpio_num_t(cs_), 0);
    gpio_set_level(gpio_num_t(dc_), 0);
    spi_transaction_t commandTransaction{};
    commandTransaction.length = 8;
    commandTransaction.tx_buffer = &command;
    lastIoError_ = spi_device_transmit(spiDevice_, &commandTransaction);
    if (lastIoError_ == ESP_OK && length) {
        gpio_set_level(gpio_num_t(dc_), 1);
        spi_transaction_t dataTransaction{};
        dataTransaction.length = length * 8;
        dataTransaction.tx_buffer = data;
        lastIoError_ = spi_device_transmit(spiDevice_, &dataTransaction);
    }
    gpio_set_level(gpio_num_t(cs_), 1);
    return lastIoError_ == ESP_OK;
}

bool Ssd1681Display::writeCommandByte(uint8_t command, uint8_t data) {
    return writeCommandData(command, &data, 1);
}

bool Ssd1681Display::setRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!w || !h) return false;
    const uint8_t xRange[] = {uint8_t(x / 8), uint8_t((x + w - 1) / 8)};
    const uint8_t yRange[] = {uint8_t(y), uint8_t(y >> 8),
                              uint8_t(y + h - 1), uint8_t((y + h - 1) >> 8)};
    return writeCommandByte(0x11, 0x03) &&
           writeCommandData(0x44, xRange, sizeof(xRange)) &&
           writeCommandData(0x45, yRange, sizeof(yRange)) &&
           writeCommandByte(0x4E, xRange[0]) &&
           writeCommandData(0x4F, &yRange[0], 2);
}

bool Ssd1681Display::waitBusy(uint32_t timeoutMs) {
    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(timeoutMs);
    while (isBusy()) {
        if (busyCallback_) busyCallback_(busyUserData_);
        if (xTaskGetTickCount() - start >= timeout) {
            lastIoError_ = ESP_ERR_TIMEOUT;
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}

bool Ssd1681Display::powerOffPanel() {
    if (!writeCommandByte(0x22, 0x83) || !writeCommand(0x20) || !waitBusy()) return false;
    return true;
}

void Ssd1681Display::reportIoError() {
    ESP_LOGE(kTag, "IO error 0x%x", unsigned(lastIoError_));
}

} // namespace display
} // namespace drivers
} // namespace ersa

#endif // ARDUINO
