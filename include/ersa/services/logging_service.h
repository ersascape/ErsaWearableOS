#pragma once

#include <stdint.h>
#include <stdarg.h>

namespace ersa {
namespace services {

/** Severity ordering used to filter messages before backend formatting. */
enum class LogLevel : uint8_t {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

/**
 * Severity-filtered logging contract for firmware and host implementations.
 *
 * The interface gives services one printf-style diagnostic sink on hardware
 * and in tests. Backends decide where messages go (serial, ring buffer, or
 * host output), while the severity threshold avoids constructing/forwarding
 * routine messages when a deployment wants quieter logs.
 */
class LoggingService {
public:
    virtual ~LoggingService() = default;

    /**
     * Format and emit a message when severity is at least the configured level.
     * `format` and arguments follow printf rules; `tag` identifies the source
     * subsystem and should be a stable short label.
     */
    virtual void log(LogLevel level, const char* tag, const char* format, ...) __attribute__((format(printf, 4, 5))) = 0;
    /** Same filtering/format behavior as log(), accepting a va_list for adapters. */
    virtual void logv(LogLevel level, const char* tag, const char* format, va_list args) = 0;

    /** Change the backend's severity threshold; lower levels are filtered out. */
    virtual void setLevel(LogLevel level) { minLevel_ = level; }
    /// Return the current minimum severity.
    virtual LogLevel getLevel() const { return minLevel_; }

    /** Return the logger installed during system composition. */
    static LoggingService& instance();
    /** Install a non-owning logger implementation before services start. */
    static void setInstance(LoggingService* instance);

protected:
    LogLevel minLevel_{LogLevel::Info};
};

} // namespace services
} // namespace ersa

#define ERSA_LOG_TRACE(tag, fmt, ...) ersa::services::LoggingService::instance().log(ersa::services::LogLevel::Trace, tag, fmt, ##__VA_ARGS__)
#define ERSA_LOG_DEBUG(tag, fmt, ...) ersa::services::LoggingService::instance().log(ersa::services::LogLevel::Debug, tag, fmt, ##__VA_ARGS__)
#define ERSA_LOG_INFO(tag, fmt, ...)  ersa::services::LoggingService::instance().log(ersa::services::LogLevel::Info, tag, fmt, ##__VA_ARGS__)
#define ERSA_LOG_WARN(tag, fmt, ...)  ersa::services::LoggingService::instance().log(ersa::services::LogLevel::Warn, tag, fmt, ##__VA_ARGS__)
#define ERSA_LOG_ERROR(tag, fmt, ...) ersa::services::LoggingService::instance().log(ersa::services::LogLevel::Error, tag, fmt, ##__VA_ARGS__)
#define ERSA_LOG_FATAL(tag, fmt, ...) ersa::services::LoggingService::instance().log(ersa::services::LogLevel::Fatal, tag, fmt, ##__VA_ARGS__)
