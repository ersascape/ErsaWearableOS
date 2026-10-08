#if defined(ARDUINO)

#include "ersa/services/storage_service.h"
#include "hal/esp32/esp32_storage.h"
#include <Preferences.h>

namespace ersa::hal {
namespace {

class Esp32Storage final : public services::StorageService {
public:
    Result<void> init() override {
        if (!opened_) opened_ = prefs_.begin("ersa_nvs", false);
        return opened_ ? Result<void>() : Result<void>(ErrorCode::IoError, "could not open NVS namespace");
    }

    bool setString(std::string_view key, std::string_view value) override {
        if (!ensureOpen()) return false;
        const std::string k(key), v(value);
        const size_t written = prefs_.putString(k.c_str(), v.c_str());
        if (!value.empty()) return written == value.size();

        // Arduino Preferences reports strlen(value), so a successful empty
        // string write returns zero just like a failed write. Verify the
        // persisted value to distinguish the successful empty-string case.
        if (written != 0) return false;
        const String stored = prefs_.getString(k.c_str(), "\x01");
        return stored.length() == 0;
    }
    std::string getString(std::string_view key, std::string_view fallback) override {
        if (!ensureOpen()) return std::string(fallback);
        const std::string k(key), def(fallback);
        const String value = prefs_.getString(k.c_str(), def.c_str());
        return value.c_str();
    }
    bool setInt(std::string_view key, int32_t value) override {
        if (!ensureOpen()) return false;
        const std::string k(key);
        return prefs_.putInt(k.c_str(), value) > 0;
    }
    int32_t getInt(std::string_view key, int32_t fallback) override {
        if (!ensureOpen()) return fallback;
        const std::string k(key);
        return prefs_.getInt(k.c_str(), fallback);
    }
    bool setBool(std::string_view key, bool value) override {
        if (!ensureOpen()) return false;
        const std::string k(key);
        return prefs_.putBool(k.c_str(), value) > 0;
    }
    bool getBool(std::string_view key, bool fallback) override {
        if (!ensureOpen()) return fallback;
        const std::string k(key);
        return prefs_.getBool(k.c_str(), fallback);
    }
    bool setBytes(std::string_view key, const void* data, size_t size) override {
        if (!ensureOpen() || (size && !data)) return false;
        const std::string k(key);
        return prefs_.putBytes(k.c_str(), data, size) == size;
    }
    size_t getBytesLength(std::string_view key) override {
        if (!ensureOpen()) return 0;
        const std::string k(key);
        return prefs_.getBytesLength(k.c_str());
    }
    size_t getBytes(std::string_view key, void* data, size_t size) override {
        if (!ensureOpen() || (size && !data)) return 0;
        const std::string k(key);
        return prefs_.getBytes(k.c_str(), data, size);
    }
    bool remove(std::string_view key) override {
        if (!ensureOpen()) return false;
        const std::string k(key);
        return prefs_.remove(k.c_str());
    }
    void clear() override { if (ensureOpen()) prefs_.clear(); }

private:
    bool ensureOpen() { return opened_ || init().isOk(); }
    Preferences prefs_;
    bool opened_{false};
};

Esp32Storage storage;

} // namespace

void installEsp32Storage() {
    services::StorageService::setInstance(&storage);
}

} // namespace ersa::hal

#endif
