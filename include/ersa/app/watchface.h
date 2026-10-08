#pragma once

#include "ersa/app/application.h"

namespace ersa {
namespace app {

/** Specialized application contract for the always-available watch screen. */
class Watchface : public Application {
public:
    /** Allow destruction through the watchface base type. */
    ~Watchface() override = default;

    /** Watchfaces share one stable ID so navigation can return to the face. */
    const char* getId() const override { return "watchface"; }
    /** Default label used for the watchface entry in navigation. */
    const char* getTitle() const override { return "Watchface"; }

    /** Draw the face into the generic canvas without choosing a panel driver. */
    virtual void render(ui::Canvas& canvas) override = 0;
};

} // namespace app
} // namespace ersa
