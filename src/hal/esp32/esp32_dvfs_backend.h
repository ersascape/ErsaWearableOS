#pragma once
#include <stddef.h>

namespace Dvfs {

/** Requested temporary CPU/APB guarantee; platform locks map each profile. */
enum class Profile : unsigned char {
    Interactive, // ESP_PM_APB_FREQ_MAX: CPU 80 MHz, APB 80 MHz
    Compute      // ESP_PM_CPU_FREQ_MAX: CPU 160 MHz
};

/** Initialize ESP-IDF power-management locks and dynamic frequency scaling. */
bool begin();
/** Acquire a profile lock and optionally log a diagnostic reason. */
bool acquire(Profile profile, const char* reason);
/** Release a previously acquired profile lock. */
void release(Profile profile, const char* reason);
/** Sample CPU frequency/residency while application work runs. */
void tick();
/** Emit a one-time report of ESP-IDF PM configuration and lock state. */
void reportPowerModes();
/** Copy the current power-mode report to a bounded caller-owned buffer. */
bool getPowerModeReport(char* buffer, size_t capacity);
// Debug-only override used by the USB control bridge. 0 restores the normal
// 40-160 MHz PM range; supported forced values are 40, 80 and 160 MHz.
bool setTestCpuFrequencyMHz(unsigned mhz);
/** Return the forced test frequency, or zero when dynamic DVFS is restored. */
unsigned testCpuFrequencyMHz();

/** RAII wrapper that balances acquire/release for one temporary profile. */
class Scope {
public:
    /** Acquire profile for the lexical scope; failed acquisition is a no-op. */
    Scope(Profile profile, const char* reason);
    /** Release profile only when the constructor successfully acquired it. */
    ~Scope();

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Profile profile_;
    const char* reason_;
    bool acquired_;
};

} // namespace Dvfs
