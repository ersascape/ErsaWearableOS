#pragma once

#include <stdint.h>
#include <atomic>

namespace ersa {
namespace services {

/// Coordinates signed-metadata checks, OTA image install, and boot validation.
class OtaService {
public:
    enum class UpdateState : uint8_t {
        Idle, Checking, UpToDate, Available, Installing, Failed
    };

    /// Start rollback validation if the bootloader selected a new image.
    void begin();

    /// Advance OTA validation and jobs from the application loop.
    void tick();

    /// Check the configured release manifest for a newer compatible image.
    bool checkForUpdate();
    /// Download, validate, and install the available image into the other slot.
    bool installUpdate();
    /// Resume radio and companion providers after a completed check.
    void resumeAfterCheck();
    /// Return the current update state.
    UpdateState updateState() const { return updateState_.load(); }
    /// Return the version being checked or installed.
    const char* updateVersion() const { return updateVersion_; }
    /// Return a concise status or failure message for the current update.
    const char* updateMessage() const;
    /// Return the active application slot name.
    const char* runningSlot() const;
    /// Return the bootloader state of the active image.
    const char* runningImageState() const;
    /// Return the firmware version embedded in the active image.
    const char* runningVersion() const;
    /// Return the alternate application slot name.
    const char* otherSlot() const;
    /// Return the bootloader state of the alternate image.
    const char* otherImageState() const;
    /// Return whether the alternate image passes bootability validation.
    bool otherSlotBootable() const;
    /// Select a validated alternate image for the next boot.
    bool selectOtherSlot();
    /// Return the installed process-wide OTA service.
    static OtaService& instance();

private:
    static void updateTask(void* context);
    void runUpdate(bool install);
    bool pendingConfirmation_{false};
    uint32_t confirmationStartedMs_{0};
    std::atomic<UpdateState> updateState_{UpdateState::Idle};
    std::atomic<bool> updateTaskActive_{false};
    std::atomic<bool> resumeComponentsRequested_{false};
    char updateVersion_[32]{};
};

} // namespace services
} // namespace ersa
