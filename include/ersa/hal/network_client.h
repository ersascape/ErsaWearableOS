#pragma once

#include "ersa/common/types.h"
#include <stdint.h>
#include <string>
#include <string_view>

namespace ersa::hal {

/** Streaming HTTP client contract for portable network services. */
class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    virtual Result<void> begin(std::string_view url, bool skipTlsVerification) = 0;
    virtual void setTimeout(uint32_t timeoutMs) = 0;
    virtual void setFollowRedirects(bool enabled) = 0;
    virtual void setBasicAuth(std::string_view user, std::string_view password) = 0;
    virtual void addHeader(std::string_view name, std::string_view value) = 0;
    virtual void collectHeader(std::string_view name) = 0;
    virtual int get() = 0;
    virtual int statusCode() const = 0;
    virtual bool hasHeader(std::string_view name) const = 0;
    virtual std::string header(std::string_view name) const = 0;
    virtual std::string body() = 0;
    virtual bool connected() const = 0;
    virtual int available() const = 0;
    virtual std::string readLine() = 0;
    virtual int lastTransportError(char* message, size_t capacity) const = 0;
    virtual void end() = 0;
};

/** NTP client contract that returns synchronized UTC without exposing SNTP. */
class INtpTimeSource {
public:
    virtual ~INtpTimeSource() = default;
    virtual bool synchronizeUtc(uint32_t timeoutMs, uint32_t& epochSeconds) = 0;
};

} // namespace ersa::hal
