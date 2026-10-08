#pragma once

#include "ersa/common/types.h"
#include <stdint.h>
#include <string>
#include <string_view>

namespace ersa::hal {

enum class HttpMethod : uint8_t { Get, Post, Any };
using HttpHandler = void (*)(void* context);

/** Request/response server contract for configuration and local web apps. */
class IHttpServer {
public:
    virtual ~IHttpServer() = default;
    virtual Result<void> begin(uint16_t port) = 0;
    virtual void route(std::string_view path, HttpMethod method, HttpHandler handler, void* context) = 0;
    virtual void routeNotFound(HttpHandler handler, void* context) = 0;
    virtual void handleClient() = 0;
    virtual void stop() = 0;
    virtual bool hasArgument(std::string_view name) const = 0;
    virtual std::string argument(std::string_view name) const = 0;
    virtual void send(uint16_t status, std::string_view contentType, std::string_view body) = 0;
};

/** Captive-DNS responder contract kept separate from product web content. */
class IDnsServer {
public:
    virtual ~IDnsServer() = default;
    virtual Result<void> begin(uint16_t port, std::string_view domain, const uint8_t address[4]) = 0;
    virtual void process() = 0;
    virtual void stop() = 0;
};

} // namespace ersa::hal
