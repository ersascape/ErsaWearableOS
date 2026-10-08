#include "ersa/services/display_manager.h"

namespace ersa {
namespace services {

static DisplayManager* s_displayManagerInstance = nullptr;

DisplayManager& DisplayManager::instance() {
    return *s_displayManagerInstance;
}

void DisplayManager::setInstance(DisplayManager* instance) {
    s_displayManagerInstance = instance;
}

DisplayManager::DisplayManager(hal::IDisplay& display)
    : display_(display) {}

Result<void> DisplayManager::init() {
    Result<void> res = display_.init();
    dirty_ = true;
    fullNeeded_ = true;
    return res;
}

void DisplayManager::markDirty(bool fullRefresh) {
    dirty_ = true;
    if (fullRefresh) {
        fullNeeded_ = true;
    }
}

bool DisplayManager::isDirty() const {
    return dirty_;
}

void DisplayManager::refresh(bool full, uint32_t currentUptimeMs) {
    refreshRect(Rect{0, 0, display_.width(), display_.height()}, full, currentUptimeMs);
}

void DisplayManager::refreshRect(const Rect& bounds, bool forceFull, uint32_t currentUptimeMs) {
    const bool doFull = forceFull || fullNeeded_ || (partialFrames_ >= FULL_REFRESH_FRAME_COUNT);
    if (doFull) display_.refresh(true);
    else display_.refreshRect(bounds);

    if (doFull) {
        partialFrames_ = 0;
    } else {
        partialFrames_++;
    }

    dirty_ = false;
    fullNeeded_ = false;
    lastRefreshTime_ = currentUptimeMs;
}

bool DisplayManager::updateIfDirty(uint32_t currentUptimeMs) {
    if (!dirty_) return false;
    if (currentUptimeMs - lastRefreshTime_ < MIN_REFRESH_INTERVAL_MS) {
        return false;
    }
    refresh(fullNeeded_, currentUptimeMs);
    return true;
}

void DisplayManager::tick(uint32_t currentUptimeMs) {
    // The panel drive voltage is already disabled by the display driver after
    // each refresh; e-paper retains its image while the MCU can enter light sleep.
    (void)currentUptimeMs;
}

uint16_t DisplayManager::getPartialFrameCount() const {
    return partialFrames_;
}

void DisplayManager::resetPartialFrameCount() {
    partialFrames_ = 0;
}

hal::IDisplay& DisplayManager::getDisplay() {
    return display_;
}

} // namespace services
} // namespace ersa
