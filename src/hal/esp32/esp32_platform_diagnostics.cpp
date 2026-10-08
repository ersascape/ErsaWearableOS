#if defined(ARDUINO)

#include "hal/esp32/esp32_platform_diagnostics.h"
#include <esp32/clk.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

namespace ersa::hal {

uint32_t Esp32PlatformDiagnostics::resetReasonCode() const {
    return static_cast<uint32_t>(esp_reset_reason());
}

const char* Esp32PlatformDiagnostics::resetReasonName(uint32_t code) const {
    switch (static_cast<esp_reset_reason_t>(code)) {
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_EXT: return "EXTERNAL";
        case ESP_RST_SW: return "SOFTWARE";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT: return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO: return "SDIO";
        default: return "UNKNOWN";
    }
}

size_t Esp32PlatformDiagnostics::freeHeapBytes() const { return esp_get_free_heap_size(); }
size_t Esp32PlatformDiagnostics::largestFreeHeapBlockBytes() const {
    return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}
uint32_t Esp32PlatformDiagnostics::cpuFrequencyMHz() const {
    return static_cast<uint32_t>(esp_clk_cpu_freq() / 1000000);
}

} // namespace ersa::hal

#endif
