#pragma once

#include <stdint.h>

namespace ersa {
namespace board {

struct BoardCapabilities {
    bool wifi{false};
    bool bluetooth{false};
    bool rtc{false};
    bool batteryGauge{false};
    bool haptics{false};
    bool touch{false};
    bool buttons{false};
};

struct DisplayConfig {
    uint16_t width{200};
    uint16_t height{200};
    bool partialRefresh{true};
    bool isEpaper{true};
};

struct BoardConfig {
    const char* name{"Generic Board"};
    BoardCapabilities capabilities;
    DisplayConfig display;
};

} // namespace board
} // namespace ersa
