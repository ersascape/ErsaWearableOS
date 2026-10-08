#pragma once

#include <stdint.h>
#include <atomic>

namespace ersa {
namespace services {

class OtaService {
public:
    enum class UpdateState : uint8_t {
        Idle, Checking, UpToDate, Available, Installing, Failed
    };

    // Start rollback validation if the bootloader selected a new image.
    void begin();

    // Advance validation from the application loop; no extra task is created.
    void tick();

    bool checkForUpdate();
    bool installUpdate();
    void resumeAfterCheck();
    UpdateState updateState() const { return updateState_.load(); }
    const char* updateVersion() const { return updateVersion_; }
    const char* updateMessage() const;
    const char* runningSlot() const;
    const char* runningImageState() const;
    const char* runningVersion() const;
    const char* otherSlot() const;
    const char* otherImageState() const;
    bool otherSlotBootable() const;
    bool selectOtherSlot();
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
