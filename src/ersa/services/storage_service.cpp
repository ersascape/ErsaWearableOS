#include "ersa/services/storage_service.h"
#include <unordered_map>
#include <cstring>

namespace ersa {
namespace services {

static StorageService* s_storageInstance = nullptr;

StorageService& StorageService::instance() {
    return *s_storageInstance;
}

void StorageService::setInstance(StorageService* instance) {
    s_storageInstance = instance;
}

#if !defined(ARDUINO)
class MemoryStorageService : public StorageService {
public:
    Result<void> init() override {
        return Result<void>();
    }

    bool setString(std::string_view key, std::string_view value) override {
        map_[std::string(key)] = std::string(value);
        return true;
    }

    std::string getString(std::string_view key, std::string_view defaultValue) override {
        auto it = map_.find(std::string(key));
        if (it != map_.end()) return it->second;
        return std::string(defaultValue);
    }

    bool setInt(std::string_view key, int32_t value) override {
        map_[std::string(key)] = std::to_string(value);
        return true;
    }

    int32_t getInt(std::string_view key, int32_t defaultValue) override {
        auto it = map_.find(std::string(key));
        if (it != map_.end()) {
            try { return std::stoi(it->second); } catch (...) {}
        }
        return defaultValue;
    }

    bool setBool(std::string_view key, bool value) override {
        map_[std::string(key)] = value ? "1" : "0";
        return true;
    }

    bool getBool(std::string_view key, bool defaultValue) override {
        auto it = map_.find(std::string(key));
        if (it != map_.end()) return it->second == "1";
        return defaultValue;
    }

    bool setBytes(std::string_view key, const void* data, size_t size) override {
        if (size && !data) return false;
        map_[std::string(key)] = size ? std::string(static_cast<const char*>(data), size) : std::string();
        return true;
    }

    size_t getBytesLength(std::string_view key) override {
        auto it = map_.find(std::string(key));
        return it == map_.end() ? 0 : it->second.size();
    }

    size_t getBytes(std::string_view key, void* data, size_t size) override {
        auto it = map_.find(std::string(key));
        if (it == map_.end() || it->second.size() != size || (size && !data)) return 0;
        if (size) std::memcpy(data, it->second.data(), size);
        return size;
    }

    bool remove(std::string_view key) override {
        return map_.erase(std::string(key)) > 0;
    }

    void clear() override {
        map_.clear();
    }

private:
    std::unordered_map<std::string, std::string> map_;
};

static MemoryStorageService s_defaultMemStorage;
#endif

// Auto-register default instance on startup
struct StorageAutoInit {
    StorageAutoInit() {
#if !defined(ARDUINO)
        StorageService::setInstance(&s_defaultMemStorage);
#endif
    }
} s_autoInit;

} // namespace services
} // namespace ersa
