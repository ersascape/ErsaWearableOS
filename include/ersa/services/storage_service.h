#pragma once

#include "ersa/common/types.h"
#include <string>
#include <string_view>
#include <stdint.h>

namespace ersa {
namespace services {

/**
 * Typed persistent key/value contract used by settings and app state.
 *
 * The interface avoids exposing NVS, filesystems, or host test storage details.
 * Values are stored by key and implementations decide the physical medium;
 * callers must inspect write results when persistence matters and use defaults
 * when keys are absent, which supports first-boot and schema migration paths.
 */
class StorageService {
public:
    virtual ~StorageService() = default;

    /** Mount/open the backend and prepare its namespace for reads and writes. */
    virtual Result<void> init() = 0;

    /** Store a string; false reports backend failure or exhausted storage. */
    virtual bool setString(std::string_view key, std::string_view value) = 0;
    /** Return the stored value or a copy of `defaultValue` when key is absent. */
    virtual std::string getString(std::string_view key, std::string_view defaultValue = "") = 0;

    /** Store a signed integer using the backend's typed representation. */
    virtual bool setInt(std::string_view key, int32_t value) = 0;
    /** Read a signed integer or return `defaultValue` for an absent key. */
    virtual int32_t getInt(std::string_view key, int32_t defaultValue = 0) = 0;

    /** Store a boolean setting; false indicates that persistence failed. */
    virtual bool setBool(std::string_view key, bool value) = 0;
    /** Read a boolean setting or return `defaultValue` when the key is absent. */
    virtual bool getBool(std::string_view key, bool defaultValue = false) = 0;

    /** Remove one key; false means no removal was made or the backend failed. */
    virtual bool remove(std::string_view key) = 0;
    /** Clear this service's namespace; use carefully because it removes all keys. */
    virtual void clear() = 0;

    /// Return the installed process-wide storage service.
    static StorageService& instance();
    /// Install the process-wide storage backend.
    static void setInstance(StorageService* instance);
};

} // namespace services
} // namespace ersa
