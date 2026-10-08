#if defined(ARDUINO)

#include "hal/esp32/esp32_web_server.h"
#include <IPAddress.h>

namespace ersa::hal {

Esp32WebServer::Esp32WebServer() : server_(80) {}

Result<void> Esp32WebServer::begin(uint16_t port) {
    if (port != 80) return Result<void>(ErrorCode::NotSupported, "ESP32 local web adapter is bound to port 80");
    server_.begin();
    return Result<void>();
}

void Esp32WebServer::route(std::string_view path, HttpMethod method, HttpHandler handler, void* context) {
    const std::string pathCopy(path);
    const auto callback = [handler, context]() { if (handler) handler(context); };
    HTTPMethod arduinoMethod = HTTP_ANY;
    if (method == HttpMethod::Get) arduinoMethod = HTTP_GET;
    else if (method == HttpMethod::Post) arduinoMethod = HTTP_POST;
    server_.on(pathCopy.c_str(), arduinoMethod, callback);
}

void Esp32WebServer::routeNotFound(HttpHandler handler, void* context) {
    server_.onNotFound([handler, context]() { if (handler) handler(context); });
}

void Esp32WebServer::handleClient() { server_.handleClient(); }
void Esp32WebServer::stop() { server_.stop(); dns_.stop(); }

bool Esp32WebServer::hasArgument(std::string_view name) const {
    const std::string key(name);
    return const_cast<WebServer&>(server_).hasArg(key.c_str());
}

std::string Esp32WebServer::argument(std::string_view name) const {
    const std::string key(name);
    const String value = const_cast<WebServer&>(server_).arg(key.c_str());
    return value.c_str();
}

void Esp32WebServer::send(uint16_t status, std::string_view contentType, std::string_view body) {
    const std::string type(contentType), response(body);
    server_.send(status, type.c_str(), response.c_str());
}

Result<void> Esp32WebServer::begin(uint16_t port, std::string_view domain, const uint8_t address[4]) {
    if (!address) return Result<void>(ErrorCode::InvalidParam, "captive DNS address is null");
    const std::string domainCopy(domain);
    const IPAddress ip(address[0], address[1], address[2], address[3]);
    dns_.start(port, domainCopy.c_str(), ip);
    return Result<void>();
}

void Esp32WebServer::process() { dns_.processNextRequest(); }

} // namespace ersa::hal

#endif
