#include "hal/esp32/esp32_dvfs_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(ARDUINO)
#include <sdkconfig.h>
#endif

#if defined(ARDUINO) && defined(CONFIG_PM_ENABLE) && CONFIG_PM_ENABLE
#include <Arduino.h>
#include <esp_attr.h>
#include <esp32/clk.h>
#include <esp_pm.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <atomic>
#include <stdio.h>
#include "core/debug_log.h"

namespace Dvfs {
namespace {

struct LockState {
    esp_pm_lock_handle_t handle{nullptr};
    uint16_t users{0};
    esp_pm_lock_type_t type;
    const char* name;
};

LockState interactive{nullptr, 0, ESP_PM_APB_FREQ_MAX, "dvfs-interactive"};
LockState compute{nullptr, 0, ESP_PM_CPU_FREQ_MAX, "dvfs-compute"};
SemaphoreHandle_t mutex = nullptr;
std::atomic<uint32_t> lastLoggedMHz{0};
uint32_t lastSampleMs = 0;
uint32_t interactiveScopes = 0;
uint32_t computeScopes = 0;
bool ready = false;
bool reportWritten = false;
RTC_DATA_ATTR unsigned testFrequencyMHz = 0;

LockState& stateFor(Profile profile) {
    return profile == Profile::Compute ? compute : interactive;
}

uint32_t cpuMHz() {
    return esp_clk_cpu_freq() / 1000000UL;
}

void logIfChanged(const char* reason) {
    const uint32_t current = cpuMHz();
    const uint32_t previous = lastLoggedMHz.exchange(current);
    if (previous && previous != current) {
        DebugLog::log("DVFS: CPU %lu -> %lu MHz (%s)",
                      static_cast<unsigned long>(previous),
                      static_cast<unsigned long>(current), reason ? reason : "IDF PM");
    }
}

bool acquireLock(Profile profile, const char* reason) {
    if (!ready || !mutex) return false;
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return false;
    LockState& state = stateFor(profile);
    bool acquired = state.handle != nullptr;
    if (acquired && state.users == 0)
        acquired = esp_pm_lock_acquire(state.handle) == ESP_OK;
    if (acquired) {
        ++state.users;
        if (profile == Profile::Compute) ++computeScopes;
        else ++interactiveScopes;
        logIfChanged(reason);
    }
    xSemaphoreGive(mutex);
    return acquired;
}

void releaseLock(Profile profile, const char* reason) {
    if (!ready || !mutex || xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return;
    LockState& state = stateFor(profile);
    if (state.users == 0) {
        DebugLog::log("DVFS: unbalanced release profile=%u", unsigned(profile));
    } else if (--state.users == 0) {
        const esp_err_t result = esp_pm_lock_release(state.handle);
        if (result != ESP_OK) {
            DebugLog::log("DVFS: lock release failed profile=%u err=0x%x",
                          unsigned(profile), unsigned(result));
        }
        logIfChanged(reason);
    }
    xSemaphoreGive(mutex);
}

} // namespace

bool begin() {
    if (ready) return true;
    mutex = xSemaphoreCreateMutex();
    if (!mutex) {
        DebugLog::log("DVFS: init failed creating scope mutex (free heap=%lu)",
                      static_cast<unsigned long>(ESP.getFreeHeap()));
        return false;
    }

    esp_err_t result = esp_pm_lock_create(interactive.type, 0, interactive.name, &interactive.handle);
    if (result != ESP_OK) {
        DebugLog::log("DVFS: interactive lock creation failed err=0x%x (%s)",
                      unsigned(result), esp_err_to_name(result));
    } else {
        result = esp_pm_lock_create(compute.type, 0, compute.name, &compute.handle);
        if (result != ESP_OK)
            DebugLog::log("DVFS: compute lock creation failed err=0x%x (%s)",
                          unsigned(result), esp_err_to_name(result));
    }
    if (result != ESP_OK) {
        DebugLog::log("DVFS: lock initialization failed err=0x%x", unsigned(result));
        if (interactive.handle) esp_pm_lock_delete(interactive.handle);
        if (compute.handle) esp_pm_lock_delete(compute.handle);
        interactive.handle = compute.handle = nullptr;
        vSemaphoreDelete(mutex);
        mutex = nullptr;
        return false;
    }

    lastLoggedMHz = cpuMHz();
    lastSampleMs = millis();
    ready = true;
    DebugLog::log("DVFS: ready profile=%u MHz (0=automatic) current=%lu MHz",
                  testFrequencyMHz, static_cast<unsigned long>(cpuMHz()));
    return true;
}

bool acquire(Profile profile, const char* reason) { return acquireLock(profile, reason); }
void release(Profile profile, const char* reason) { releaseLock(profile, reason); }

void tick() {
    if (!ready || uint32_t(millis() - lastSampleMs) < 1000) return;
    lastSampleMs = millis();
    logIfChanged("ESP-IDF PM / peripheral lock");
}

void reportPowerModes() {
    if (!ready || reportWritten) return;
    reportWritten = true;
    DebugLog::log("DVFS: use-case scopes so far interactive=%lu compute=%lu; active-task frequency can remain at CPU_MAX",
                  static_cast<unsigned long>(interactiveScopes),
                  static_cast<unsigned long>(computeScopes));
#if defined(CONFIG_PM_PROFILING) && CONFIG_PM_PROFILING
    char* report = nullptr;
    size_t reportSize = 0;
    FILE* stream = open_memstream(&report, &reportSize);
    if (!stream) {
        DebugLog::log("DVFS: cannot allocate PM profile report");
        return;
    }
    const esp_err_t result = esp_pm_dump_locks(stream);
    fclose(stream);
    if (result != ESP_OK) {
        DebugLog::log("DVFS: PM profile report failed err=0x%x", unsigned(result));
        free(report);
        return;
    }

    DebugLog::log("DVFS: ESP-IDF PM residency and lock report follows");
    for (size_t start = 0; start < reportSize;) {
        size_t end = start;
        while (end < reportSize && report[end] != '\n') ++end;
        if (end > start) {
            report[end] = '\0';
            DebugLog::log("DVFS PM: %s", report + start);
        }
        start = end + 1;
    }
    free(report);
#else
    DebugLog::log("DVFS: PM profiling is disabled; idle transition details unavailable");
#endif
}

bool getPowerModeReport(char* buffer, size_t capacity) {
    if (!buffer || capacity == 0 || !ready) return false;
#if defined(CONFIG_PM_PROFILING) && CONFIG_PM_PROFILING
    char* report = nullptr;
    size_t reportSize = 0;
    FILE* stream = open_memstream(&report, &reportSize);
    if (!stream) return false;
    const esp_err_t result = esp_pm_dump_locks(stream);
    fclose(stream);
    if (result != ESP_OK || !report) {
        free(report);
        return false;
    }
    const size_t copySize = reportSize < capacity - 1 ? reportSize : capacity - 1;
    memcpy(buffer, report, copySize);
    buffer[copySize] = '\0';
    free(report);
    return reportSize < capacity;
#else
    strlcpy(buffer, "ESP-IDF PM profiling is disabled", capacity);
    return false;
#endif
}

bool setTestCpuFrequencyMHz(unsigned mhz) {
    if (!ready || !mutex || (mhz != 0 && mhz != 40 && mhz != 80 && mhz != 160)) return false;
    // PM policy is configured once, before BLE and peripheral locks start.
    // Reconfiguring it live can race the radio controller and active locks.
    // Keep the requested profile in RTC memory and apply it on a controlled reboot.
    testFrequencyMHz = mhz;
    DebugLog::log("DVFS: CPU test profile %u MHz queued for controlled reboot", mhz);
    return true;
}

unsigned testCpuFrequencyMHz() { return testFrequencyMHz; }

Scope::Scope(Profile profile, const char* reason)
    : profile_(profile), reason_(reason), acquired_(acquireLock(profile, reason)) {}

Scope::~Scope() {
    if (acquired_) releaseLock(profile_, reason_);
}

} // namespace Dvfs

#else

namespace Dvfs {
bool begin() { return false; }
bool acquire(Profile, const char*) { return false; }
void release(Profile, const char*) {}
void tick() {}
void reportPowerModes() {}
bool getPowerModeReport(char* buffer, size_t capacity) {
    if (buffer && capacity) strlcpy(buffer, "ESP-IDF PM profiling unavailable", capacity);
    return false;
}
bool setTestCpuFrequencyMHz(unsigned mhz) { (void)mhz; return false; }
unsigned testCpuFrequencyMHz() { return 0; }
Scope::Scope(Profile profile, const char* reason)
    : profile_(profile), reason_(reason), acquired_(false) {
    (void)profile_; (void)reason_;
}
Scope::~Scope() = default;
} // namespace Dvfs

#endif
