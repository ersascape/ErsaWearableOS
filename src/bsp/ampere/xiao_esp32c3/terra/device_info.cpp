#include "ersa/board/board.h"

namespace ersa {
namespace board {
namespace {
// Keep this aggregate initializer as the canonical per-device identity. The
// release manifest generator reads these same three ordered fields.
constexpr ersa::board::DeviceInfo kDeviceInfo{
    "Ampere Terra",
    "terra",
    "Ampere Works"
};
} // namespace

const DeviceInfo& terraDeviceInfo() {
    return kDeviceInfo;
}

} // namespace board
} // namespace ersa
