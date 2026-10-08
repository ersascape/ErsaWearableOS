#pragma once

#include "ersa/common/types.h"
#include <string>
#include <string_view>
#include <stdint.h>

namespace ersa {
namespace services {

/// Key/value persistence contract used by settings and app state.
class StorageService {
public:
    virtual ~StorageService() = default;

    /// Initialize the persistent storage backend.
    virtual Result<void> init() = 0;

    /// Store a UTF-8 string value for a key.
    virtual bool setString(std::string_view key, std::string_view value) = 0;
    /// Read a string value, returning the supplied default when absent.
    virtual std::string getString(std::string_view key, std::string_view defaultValue = "") = 0;

    /// Store a signed 32-bit integer value for a key.
    virtual bool setInt(std::string_view key, int32_t value) = 0;
    /// Read a signed 32-bit integer value, returning the default when absent.
    virtual int32_t getInt(std::string_view key, int32_t defaultValue = 0) = 0;

    /// Store a boolean value for a key.
    virtual bool setBool(std::string_view key, bool value) = 0;
    /// Read a boolean value, returning the default when absent.
    virtual bool getBool(std::string_view key, bool defaultValue = false) = 0;

    /// Remove one key and report whether the backend accepted the operation.
    virtual bool remove(std::string_view key) = 0;
    /// Remove all values managed by this storage backend.
    virtual void clear() = 0;

    /// Return the installed process-wide storage service.
    static StorageService& instance();
    /// Install the process-wide storage backend.
    static void setInstance(StorageService* instance);
};

} // namespace services
} // namespace ersa
