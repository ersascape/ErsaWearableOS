#pragma once
#include <stddef.h>

namespace Dvfs {

enum class Profile : unsigned char {
    Interactive, // ESP_PM_APB_FREQ_MAX: CPU 80 MHz, APB 80 MHz
    Compute      // ESP_PM_CPU_FREQ_MAX: CPU 160 MHz
};

bool begin();
bool acquire(Profile profile, const char* reason);
void release(Profile profile, const char* reason);
void tick(); // Samples CPU frequency while application code is running.
void reportPowerModes(); // One-time ESP-IDF PM lock and frequency residency report.
bool getPowerModeReport(char* buffer, size_t capacity);
// Debug-only override used by the USB control bridge. 0 restores the normal
// 40-160 MHz PM range; supported forced values are 40, 80 and 160 MHz.
bool setTestCpuFrequencyMHz(unsigned mhz);
unsigned testCpuFrequencyMHz();

class Scope {
public:
    Scope(Profile profile, const char* reason);
    ~Scope();

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Profile profile_;
    const char* reason_;
    bool acquired_;
};

} // namespace Dvfs
