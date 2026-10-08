#pragma once

#if defined(ARDUINO)
#include "ersa/hal/network_client.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <array>

namespace ersa::hal {

/** Arduino HTTPClient/WiFiClientSecure adapter for IHttpClient. */
class Esp32HttpClient final : public IHttpClient {
public:
    Result<void> begin(std::string_view url, bool skipTlsVerification) override;
    void setTimeout(uint32_t timeoutMs) override;
    void setFollowRedirects(bool enabled) override;
    void setBasicAuth(std::string_view user, std::string_view password) override;
    void addHeader(std::string_view name, std::string_view value) override;
    void collectHeader(std::string_view name) override;
    int get() override;
    int statusCode() const override;
    bool hasHeader(std::string_view name) const override;
    std::string header(std::string_view name) const override;
    std::string body() override;
    bool connected() const override;
    int available() const override;
    std::string readLine() override;
    int lastTransportError(char* message, size_t capacity) const override;
    void end() override;
private:
    WiFiClientSecure client_;
    HTTPClient http_;
    std::array<std::array<char, 40>, 8> headerNames_{};
    std::array<const char*, 8> headerPointers_{};
    size_t headerCount_{0};
    int statusCode_{0};
};

/** ESP-IDF SNTP synchronization adapter returning UTC epoch seconds. */
class Esp32NtpTimeSource final : public INtpTimeSource {
public:
    bool synchronizeUtc(uint32_t timeoutMs, uint32_t& epochSeconds) override;
};

} // namespace ersa::hal
#endif
