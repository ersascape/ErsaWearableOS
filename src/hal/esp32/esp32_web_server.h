#pragma once

#if defined(ARDUINO)
#include "ersa/hal/http_server.h"
#include <DNSServer.h>
#include <WebServer.h>

namespace ersa::hal {

/** Arduino WebServer and DNSServer implementation of the generic local-web HALs. */
class Esp32WebServer final : public IHttpServer, public IDnsServer {
public:
    Esp32WebServer();
    Result<void> begin(uint16_t port) override;
    void route(std::string_view path, HttpMethod method, HttpHandler handler, void* context) override;
    void routeNotFound(HttpHandler handler, void* context) override;
    void handleClient() override;
    void stop() override;
    bool hasArgument(std::string_view name) const override;
    std::string argument(std::string_view name) const override;
    void send(uint16_t status, std::string_view contentType, std::string_view body) override;
    Result<void> begin(uint16_t port, std::string_view domain, const uint8_t address[4]) override;
    void process() override;

private:
    WebServer server_;
    DNSServer dns_;
};

} // namespace ersa::hal
#endif
