#pragma once

#include <stdint.h>
#include <stdarg.h>

namespace ersa {
namespace services {

enum class LogLevel : uint8_t {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

/// Severity-filtered logging contract for firmware and host implementations.
class LoggingService {
public:
    virtual ~LoggingService() = default;

    /// Log a printf-style message when its severity meets the configured level.
    virtual void log(LogLevel level, const char* tag, const char* format, ...) __attribute__((format(printf, 4, 5))) = 0;
    /// Log a printf-style message from an existing variadic argument list.
    virtual void logv(LogLevel level, const char* tag, const char* format, va_list args) = 0;

    /// Set the minimum severity that will be emitted.
    virtual void setLevel(LogLevel level) { minLevel_ = level; }
    /// Return the current minimum severity.
    virtual LogLevel getLevel() const { return minLevel_; }

    /// Return the installed process-wide logger.
    static LoggingService& instance();
    /// Install the process-wide logger implementation.
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
