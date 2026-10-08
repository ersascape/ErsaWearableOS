#include "core/watch_config.h"
#include "core/debug_log.h"
#include "core/usb_control.h"
#include "ersa/board/board.h"
#include "ersa/services/ota_service.h"
#include "ui/watch_ui.h"

#include <esp_log.h>

#if !defined(CONFIG_IDF_TARGET_ESP32C3)
#error "This project requires an ESP32-C3 board."
#endif

void setup() {
    // Arduino's millivolt ADC helper reapplies the GPIO mode for each sample;
    // the IDF GPIO driver's INFO message makes the periodic battery read look
    // like a reboot loop in serial logs. Keep warnings and errors visible.
    esp_log_level_set("gpio", ESP_LOG_WARN);
    // Logs use USB Serial/JTAG. UART0 GPIO20/21 remain assigned to the EPD.
    DebugLog::begin();
    // Keep vendor Wi-Fi/Bluetooth chatter to warnings/errors; the application
    // ring retains concise state changes and actionable diagnostics.
    esp_log_level_set("*", ESP_LOG_WARN);
    DebugLog::log("BOOT starting config");
    WatchConfig::begin();
    auto& powerHal = ersa::board::Board::current().getPowerManagement();
    powerHal.initialize();
    DebugLog::log("BOOT starting display");
    WatchUi::begin();
    UsbControl::begin();
    DebugLog::log("BOOT ready");
    ersa::services::OtaService::instance().begin();
}

void loop() {
    ersa::board::Board::current().getPowerManagement().tick();
    DebugLog::tick();
    UsbControl::tick();
    WatchUi::tick();
    ersa::services::OtaService::instance().tick();
}
