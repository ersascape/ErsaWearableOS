#include "ersa/board/board.h"
#include <sdkconfig.h>

namespace ersa {
namespace board {
namespace {
// Identity describes Ampere's Terra product; the selected XIAO target remains
// separate so firmware and OTA metadata can distinguish C3 from C6 builds.
constexpr ersa::board::DeviceInfo kDeviceInfo{
    "Ampere Terra",
    "terra",
    "Ampere Works",
#if defined(CONFIG_IDF_TARGET_ESP32C6)
    "xiao_esp32c6"
#else
    "xiao_esp32c3"
#endif
};
} // namespace

const DeviceInfo& terraDeviceInfo() {
    return kDeviceInfo;
}

} // namespace board
} // namespace ersa
