#pragma once

#include <stdint.h>
#include <atomic>

namespace ersa {
namespace services {

/**
 * Coordinates release checks, inactive-slot image installation, and rollback safety.
 *
 * Network download runs outside the UI tick. The bootloader records whether an
 * image is pending validation; this service confirms it only after firmware
 * startup checks pass. Keeping slot selection and confirmation here prevents
 * app code from writing arbitrary flash addresses or bypassing rollback.
 */
class OtaService {
public:
    /** User-visible OTA lifecycle state, updated atomically by worker tasks. */
    enum class UpdateState : uint8_t {
        Idle, Checking, UpToDate, Available, Installing, Failed
    };

    /** Inspect boot metadata and begin the pending-image validation window. */
    void begin();

    /** Confirm a healthy boot or apply worker completion without blocking UI. */
    void tick();

    /** Start a bounded background manifest check; inspect updateState() for result. */
    bool checkForUpdate();
    /**
     * Start image download and validation into the inactive OTA slot.
     * The active image remains selected until bootloader metadata is updated.
     */
    bool installUpdate();
    /** Resume suspended providers after check/install work releases resources. */
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
    /** Select the alternate slot only after verifying its image state is bootable. */
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
