#include "app_call.h"
#include "ersa/services/bluetooth_manager.h"
#include "ersa/app/application_manager.h"
#include "ui/text_layout.h"
#include "core/debug_log.h"
#include <stdio.h>

namespace AppCall {

namespace {
size_t selectedRecent = 0;
}

void begin() {}

bool isCallActiveOrIncoming() {
    auto state = ersa::services::BluetoothManager::instance().getCallState();
    return (state == ersa::services::CallState::Incoming || state == ersa::services::CallState::Active);
}

bool onButton(Buttons::Event event) {
    auto& bleMgr = ersa::services::BluetoothManager::instance();
    auto state = bleMgr.getCallState();
    if (event == Buttons::Event::None) return false;

    if (state == ersa::services::CallState::Incoming) {
        if (event == Buttons::Event::Next) {
            // B1 accepts an incoming call.
            DebugLog::log("CALL: B1 pressed -> Accept call");
            bleMgr.acceptCall();
            return true;
        } else if (event == Buttons::Event::Action || event == Buttons::Event::ActionLong) {
            // B2 = DECLINE / HANG UP Call
            DebugLog::log("CALL: B2 pressed -> Decline call");
            bleMgr.rejectCall();
            ersa::app::ApplicationManager::instance().switchTo("watchface_clock");
            return true;
        } else if (event == Buttons::Event::Home) {
            ersa::app::ApplicationManager::instance().switchTo("watchface_clock");
            return true;
        }
    } else if (state == ersa::services::CallState::Active) {
        if ((event == Buttons::Event::Action || event == Buttons::Event::ActionLong) && bleMgr.canHangup()) {
            // B2 = HANG UP Call
            DebugLog::log("CALL: B2 pressed -> Hang up call");
            bleMgr.hangupCall();
            ersa::app::ApplicationManager::instance().switchTo("watchface_clock");
            return true;
        } else if (event == Buttons::Event::Home) {
            // Hold B1 to minimize the active call to the watchface.
            ersa::app::ApplicationManager::instance().switchTo("watchface_clock");
            return true;
        }
    } else {
        if (event == Buttons::Event::Next) {
            // B1 scrolls up through recent calls.
            const size_t count = bleMgr.getRecentCallCount();
            if (count) selectedRecent = (selectedRecent + count - 1) % count;
            return true;
        } else if (event == Buttons::Event::Action) {
            // B2 scrolls down through recent calls.
            const size_t count = bleMgr.getRecentCallCount();
            if (count) selectedRecent = (selectedRecent + 1) % count;
            return true;
        } else if (event == Buttons::Event::ActionLong) {
            if (bleMgr.canDial() && bleMgr.getRecentCallCount() > 0 &&
                bleMgr.getRecentCall(selectedRecent).number[0]) {
                const auto& recent = bleMgr.getRecentCall(selectedRecent);
                DebugLog::log("CALL: hold B2 -> dial recent %s", recent.name);
                bleMgr.dialRecent(selectedRecent);
                return true;
            }
            return true;
        } else if (event == Buttons::Event::Home) {
            ersa::app::ApplicationManager::instance().switchTo("app_drawer");
            return true;
        }
    }

    return false;
}

void render(ersa::hal::IDisplay& display) {
    auto& ble = ersa::services::BluetoothManager::instance();
    const auto state = ble.getCallState();
    using ersa::services::CallState;
    display.fillScreen(0);
    display.setTextColor(1);
    display.setTextWrap(false);
    display.setFont(ersa::hal::FontFace::MiSansBold10);
    display.setCursor(18, 24);
    display.print("calls");
    display.setFont(ersa::hal::FontFace::MiSansRegular8);

    if (state != CallState::Incoming && state != CallState::Active) {
        WatchText::line(display, ble.isConnected() ? "recent contacts" : "connect from status", 18, 47, 166);
        const size_t count = ble.getRecentCallCount();
        if (!count) {
            display.setFont(ersa::hal::FontFace::MiSansRegular10);
            WatchText::line(display, "no recent calls", 18, 85, 166);
            display.setFont(ersa::hal::FontFace::MiSansRegular8);
            WatchText::line(display, "incoming calls appear here", 18, 111, 166);
        }
        if (count) {
            selectedRecent %= count;
            const auto& call = ble.getRecentCall(selectedRecent);
            display.setFont(ersa::hal::FontFace::MiSansBold10);
            WatchText::line(display, call.name[0] ? call.name : "unknown", 18, 89, 164);
            display.setFont(ersa::hal::FontFace::MiSansRegular8);
            WatchText::line(display, call.number, 18, 113, 164);
            char position[20];
            snprintf(position, sizeof(position), "%u / %u", unsigned(selectedRecent + 1), unsigned(count));
            WatchText::line(display, position, 18, 145, 164);
        }
        const bool selectedDialable = count && ble.getRecentCall(selectedRecent).number[0];
        const char* dialHint = !count ? "no recent number" :
                               !selectedDialable ? "number unavailable" :
                               !ble.canDial() ? "connect phone to dial" : "hold b2: dial";
        WatchText::line(display, "b1 up   b2 down", 18, 153, 166);
        WatchText::line(display, dialHint, 18, 168, 166);
        WatchText::line(display, "hold b1: menu", 18, 186, 166);
        return;
    }

    WatchText::line(display, state == CallState::Incoming ? "incoming call" :
                    state == CallState::Active ? "in call" : "ringing finished", 18, 47, 166);
    display.setFont(ersa::hal::FontFace::MiSansBold10);
    WatchText::line(display, ble.getCallerName()[0] ? ble.getCallerName() : "unknown caller", 18, 89, 164);
    display.setFont(ersa::hal::FontFace::MiSansRegular8);
    WatchText::line(display, ble.getCallerNumber(), 18, 114, 164);
    display.drawFastHLine(18, 135, 164, 1);
    if (state == CallState::Active) {
        char duration[20];
        const uint32_t seconds = ble.getCallDurationSec();
        snprintf(duration, sizeof(duration), "%02u:%02u", unsigned(seconds / 60), unsigned(seconds % 60));
        WatchText::line(display, duration, 18, 153, 164);
    }
    if (state == CallState::Incoming) {
        WatchText::line(display, "b1: accept", 18, 168, 166);
        WatchText::line(display, "b2: decline", 18, 186, 166);
    } else if (state == CallState::Active) {
        WatchText::line(display, ble.canHangup() ? "b2: end call" : "manage call on phone", 18, 168, 166);
        WatchText::line(display, "b1: menu", 18, 186, 166);
    }
}

} // namespace AppCall
