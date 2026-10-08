#pragma once

#include "ersa/services/time_service.h"
#include "ersa/services/power_manager.h"
#include "ersa/services/network_manager.h"
#include "ersa/services/storage_service.h"
#include "ersa/services/settings_service.h"
#include "ersa/services/logging_service.h"
#include "ersa/events/event_bus.h"

namespace ersa {
namespace system {

/** Return the installed wall-clock coordinator. */
inline services::TimeService& time() {
    return services::TimeService::instance();
}

/** Return the installed battery, wake-lock, and sleep policy service. */
inline services::PowerManager& power() {
    return services::PowerManager::instance();
}

/** Return the installed Wi-Fi lease manager. */
inline services::NetworkManager& network() {
    return services::NetworkManager::instance();
}

/** Return the installed typed key/value storage service. */
inline services::StorageService& storage() {
    return services::StorageService::instance();
}

/** Return the installed typed user-settings service. */
inline services::SettingsService& settings() {
    return services::SettingsService::instance();
}

/** Return the installed severity-filtered logger. */
inline services::LoggingService& logger() {
    return services::LoggingService::instance();
}

/** Return the process-wide normalized event bus. */
inline events::EventBus& events() {
    return events::EventBus::instance();
}

} // namespace system
} // namespace ersa
