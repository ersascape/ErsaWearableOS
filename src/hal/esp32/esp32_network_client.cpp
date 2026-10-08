#if defined(ARDUINO)

#include "hal/esp32/esp32_network_client.h"
#include <Arduino.h>
#include "ersa/config/system_defaults.h"
#include <esp_sntp.h>
#include <ctime>
#include <cstring>

namespace ersa::hal {
namespace {
volatile bool sntpSynced = false;
void onSntpSync(struct timeval*) { sntpSynced = true; }
}

Result<void> Esp32HttpClient::begin(std::string_view url, bool skipTlsVerification) {
    http_.end();
    headerCount_ = 0;
    statusCode_ = 0;
    const std::string address(url);
    if (skipTlsVerification) client_.setInsecure();
    const bool started = address.rfind("https://", 0) == 0
        ? http_.begin(client_, address.c_str())
        : http_.begin(address.c_str());
    return started ? Result<void>() : Result<void>(ErrorCode::IoError, "HTTP client could not start request");
}

void Esp32HttpClient::setTimeout(uint32_t timeoutMs) { http_.setTimeout(timeoutMs); client_.setTimeout(timeoutMs); }
void Esp32HttpClient::setFollowRedirects(bool enabled) {
    http_.setFollowRedirects(enabled ? HTTPC_STRICT_FOLLOW_REDIRECTS : HTTPC_DISABLE_FOLLOW_REDIRECTS);
}
void Esp32HttpClient::setBasicAuth(std::string_view user, std::string_view password) {
    const std::string u(user), p(password);
    http_.setAuthorization(u.c_str(), p.c_str());
}
void Esp32HttpClient::addHeader(std::string_view name, std::string_view value) {
    const std::string n(name), v(value);
    http_.addHeader(n.c_str(), v.c_str());
}
void Esp32HttpClient::collectHeader(std::string_view name) {
    if (headerCount_ >= headerNames_.size()) return;
    auto& destination = headerNames_[headerCount_];
    const size_t count = name.size() < destination.size() - 1 ? name.size() : destination.size() - 1;
    memcpy(destination.data(), name.data(), count);
    destination[count] = '\0';
    headerPointers_[headerCount_] = destination.data();
    ++headerCount_;
    http_.collectHeaders(headerPointers_.data(), headerCount_);
}
int Esp32HttpClient::get() { statusCode_ = http_.GET(); return statusCode_; }
int Esp32HttpClient::statusCode() const { return statusCode_; }
bool Esp32HttpClient::hasHeader(std::string_view name) const {
    const std::string n(name);
    return const_cast<HTTPClient&>(http_).hasHeader(n.c_str());
}
std::string Esp32HttpClient::header(std::string_view name) const {
    const std::string n(name);
    const String value = const_cast<HTTPClient&>(http_).header(n.c_str());
    return value.c_str();
}
std::string Esp32HttpClient::body() { return http_.getString().c_str(); }
bool Esp32HttpClient::connected() const { return const_cast<HTTPClient&>(http_).connected(); }
int Esp32HttpClient::available() const {
    WiFiClient* stream = const_cast<HTTPClient&>(http_).getStreamPtr();
    return stream ? stream->available() : 0;
}
std::string Esp32HttpClient::readLine() {
    WiFiClient* stream = http_.getStreamPtr();
    if (!stream) return {};
    String line = stream->readStringUntil('\n');
    line.trim();
    return line.c_str();
}
int Esp32HttpClient::lastTransportError(char* message, size_t capacity) const {
    return const_cast<WiFiClientSecure&>(client_).lastError(message, capacity);
}
void Esp32HttpClient::end() { http_.end(); }

bool Esp32NtpTimeSource::synchronizeUtc(uint32_t timeoutMs, uint32_t& epochSeconds) {
    sntpSynced = false;
    if (esp_sntp_enabled()) esp_sntp_stop();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    for (size_t i = 0; i < config::NUM_NTP_SERVERS && i < 3; ++i)
        esp_sntp_setservername(i, config::DEFAULT_NTP_SERVERS[i]);
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
    sntp_set_time_sync_notification_cb(onSntpSync);
    esp_sntp_init();
    const uint32_t startMs = millis();
    while (uint32_t(millis() - startMs) < timeoutMs) {
        const time_t now = time(nullptr);
        if ((sntpSynced || sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED || now >= 1700000000) && now >= 1700000000) {
            epochSeconds = static_cast<uint32_t>(now);
            return true;
        }
        delay(100);
    }
    return false;
}

} // namespace ersa::hal

#endif
