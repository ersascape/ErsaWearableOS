#include "app_now_playing.h"
#include "ersa/services/bluetooth_manager.h"
#include "ersa/app/application_manager.h"
#include "ui/text_layout.h"
#include "core/debug_log.h"

namespace AppNowPlaying {

void begin() {}

bool onButton(Buttons::Event event) {
    auto& bleMgr = ersa::services::BluetoothManager::instance();

    if (event != Buttons::Event::Home && !bleMgr.mediaReady()) return false;

    if (event == Buttons::Event::Next) {
        // B1 Click = Next Track
        DebugLog::log("MEDIA: Next track");
        bleMgr.mediaNext();
        return true;
    } else if (event == Buttons::Event::Action) {
        // B2 Click = Play / Pause Toggle
        DebugLog::log("MEDIA: Toggle play/pause");
        bleMgr.mediaToggle();
        return true;
    } else if (event == Buttons::Event::Previous) {
        // B1 Double Click = Previous Track
        DebugLog::log("MEDIA: Previous track");
        bleMgr.mediaPrevious();
        return true;
    } else if (event == Buttons::Event::Home) {
        // B1 Long = Exit to Drawer
        ersa::app::ApplicationManager::instance().switchTo("app_drawer");
        return true;
    } else if (event == Buttons::Event::ActionLong) {
        // B2 Long = Previous Track
        DebugLog::log("MEDIA: Previous track (hold B2)");
        bleMgr.mediaPrevious();
        return true;
    }
    return false;
}

void render(ersa::hal::IDisplay& display) {
    auto& ble = ersa::services::BluetoothManager::instance();
    const bool ready = ble.mediaReady();
    const bool track = ready && ble.getMediaTitle()[0];
    display.fillScreen(0);
    display.setTextColor(1);
    display.setTextWrap(false);
    display.setFont(ersa::hal::FontFace::MiSansBold10);
    display.setCursor(18, 24);
    display.print("now playing");

    display.setFont(ersa::hal::FontFace::MiSansRegular8);
    WatchText::line(display, !ble.isConnected() ? "connect from status" :
                    !ready ? "waiting for music service" :
                    ble.isPlaying() ? "playing on phone" : "paused on phone", 18, 47, 166);

    display.setFont(ersa::hal::FontFace::MiSansBold10);
    WatchText::line(display, track ? ble.getMediaTitle() : "no track", 18, 89, 164);
    display.setFont(ersa::hal::FontFace::MiSansRegular8);
    WatchText::line(display, track ? ble.getMediaArtist() : "play music on phone", 18, 114, 164);
    display.drawFastHLine(18, 135, 164, 1);
    WatchText::line(display, "hold b1: back", 18, 153, 164);

    display.setFont(ersa::hal::FontFace::MiSansRegular8);
    WatchText::line(display, "b1: next / hold b2: prev", 18, 168, 166);
    WatchText::line(display, ble.isPlaying() ? "b2: pause" : "b2: play", 18, 186, 166);
}

} // namespace AppNowPlaying
