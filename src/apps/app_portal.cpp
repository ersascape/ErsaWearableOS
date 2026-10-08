#include "app_portal.h"
#include "core/watch_config.h"
#include "core/net_sync.h"
#include "core/debug_log.h"
#include "ersa/board/board.h"
#include <cstdlib>
#include <string>

namespace AppPortal {

namespace {
bool apRunning = false;
uint32_t apStartTime = 0;
auto& server() { return ersa::board::Board::current().getHttpServer(); }
auto& dnsServer() { return ersa::board::Board::current().getDnsServer(); }
bool routesInstalled = false;

const char HTML_PAGE[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Ersa Watch Settings</title>
<style>
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;margin:0;padding:16px;background:#f4f4f7;color:#222;}
.card{background:#fff;border-radius:12px;padding:20px;box-shadow:0 2px 8px rgba(0,0,0,0.08);max-width:440px;margin:0 auto;}
h2{margin-top:0;color:#111;font-size:22px;border-bottom:2px solid #eee;padding-bottom:10px;}
h3{margin:18px 0 8px 0;font-size:16px;color:#444;}
label{display:block;margin:10px 0 4px 0;font-weight:600;font-size:13px;color:#555;}
input[type=text],input[type=password],select{width:100%;box-sizing:border-box;padding:10px;border:1px solid #ccc;border-radius:6px;font-size:14px;}
.btn{display:block;width:100%;box-sizing:border-box;background:#0066cc;color:#fff;border:none;padding:12px;border-radius:6px;font-size:15px;font-weight:bold;margin-top:16px;cursor:pointer;}
.btn-alt{background:#28a745;}
.btn-stop{background:#dc3545;}
.note{font-size:12px;color:#888;margin-top:4px;}
</style>
</head>
<body>
<div class="card">
<h2>Watch Settings</h2>
<form action="/save" method="POST">
<h3>Wi-Fi</h3>
<label>Network SSID</label>
<input type="text" name="ssid" value="%SSID%" required>
<label>Password</label>
<input type="password" name="pass" value="%PASS%">

<h3>CalDAV (Nextcloud / Cloud)</h3>
<label>Server URL</label>
<input type="text" name="dav_srv" value="%DAV_SRV%" placeholder="https://cloud.example.com/remote.php/dav">
<label>Username</label>
<input type="text" name="dav_usr" value="%DAV_USR%">
<label>App Password / Token</label>
<input type="password" name="dav_pwd" value="%DAV_PWD%">

<label>Events Calendar</label>
<div style="display:flex;gap:6px;margin-bottom:6px;">
<button type="button" onclick="document.getElementById('cal_input').value='personal'" style="padding:4px 8px;font-size:12px;border-radius:4px;border:1px solid #ccc;background:#eee;cursor:pointer;">Personal</button>
<button type="button" onclick="document.getElementById('cal_input').value='work'" style="padding:4px 8px;font-size:12px;border-radius:4px;border:1px solid #ccc;background:#eee;cursor:pointer;">Work</button>
<button type="button" onclick="document.getElementById('cal_input').value='tasks'" style="padding:4px 8px;font-size:12px;border-radius:4px;border:1px solid #ccc;background:#eee;cursor:pointer;">Tasks</button>
</div>
<input type="text" id="cal_input" name="dav_cal" value="%DAV_CAL%" placeholder="personal or work or tasks or full URL">

<label>Tasks / Todo Calendar</label>
<div style="display:flex;gap:6px;margin-bottom:6px;">
<button type="button" onclick="document.getElementById('tod_input').value='personal'" style="padding:4px 8px;font-size:12px;border-radius:4px;border:1px solid #ccc;background:#eee;cursor:pointer;">Personal</button>
<button type="button" onclick="document.getElementById('tod_input').value='tasks'" style="padding:4px 8px;font-size:12px;border-radius:4px;border:1px solid #ccc;background:#eee;cursor:pointer;">Tasks</button>
<button type="button" onclick="document.getElementById('tod_input').value='work'" style="padding:4px 8px;font-size:12px;border-radius:4px;border:1px solid #ccc;background:#eee;cursor:pointer;">Work</button>
</div>
<input type="text" id="tod_input" name="dav_tod" value="%DAV_TOD%" placeholder="personal or tasks or work or full URL">

<h3>Clock & Time Format</h3>
<label>Time Display Mode</label>
<select name="fmt_24h">
<option value="0" %FMT_12H%>12-Hour (12:26 AM / PM)</option>
<option value="1" %FMT_24H%>24-Hour (00:26)</option>
</select>

<label>Timezone Offset (Minutes)</label>
<select name="tz">
<option value="330" %TZ_330%>+05:30 (India Standard Time)</option>
<option value="0" %TZ_0%>UTC (GMT / London)</option>
<option value="-300" %TZ_M300%>-05:00 (US Eastern Time)</option>
<option value="-480" %TZ_M480%>-08:00 (US Pacific Time)</option>
<option value="60" %TZ_60%>+01:00 (Central European Time)</option>
<option value="120" %TZ_120%>+02:00 (Eastern European Time)</option>
<option value="480" %TZ_480%>+08:00 (Singapore / China)</option>
<option value="540" %TZ_540%>+09:00 (Japan / Korea)</option>
</select>

<button type="submit" class="btn">Save Settings</button>
<button type="submit" name="sync_ntp" value="1" class="btn btn-alt">Save & Sync NTP Time</button>
</form>
<form action="/stop" method="POST">
<button type="submit" class="btn btn-stop">Turn Off Hotspot</button>
</form>
</div>
</body>
</html>
)rawliteral";

void replaceAll(std::string& text, const char* token, const char* value) {
    const std::string from(token), to(value ? value : "");
    size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
}

std::string renderHtml() {
    const auto& cfg = WatchConfig::get();
    std::string s(HTML_PAGE);
    replaceAll(s, "%SSID%", cfg.wifiSsid);
    replaceAll(s, "%PASS%", cfg.wifiPass);
    replaceAll(s, "%DAV_SRV%", cfg.caldavServer);
    replaceAll(s, "%DAV_USR%", cfg.caldavUser);
    replaceAll(s, "%DAV_PWD%", cfg.caldavPass);
    replaceAll(s, "%DAV_CAL%", cfg.caldavCalendar);
    replaceAll(s, "%DAV_TOD%", cfg.caldavTodoPath);

    replaceAll(s, "%FMT_12H%", !cfg.militaryTime ? "selected" : "");
    replaceAll(s, "%FMT_24H%", cfg.militaryTime ? "selected" : "");

    replaceAll(s, "%TZ_330%", cfg.timezoneOffsetMin == 330 ? "selected" : "");
    replaceAll(s, "%TZ_0%", cfg.timezoneOffsetMin == 0 ? "selected" : "");
    replaceAll(s, "%TZ_M300%", cfg.timezoneOffsetMin == -300 ? "selected" : "");
    replaceAll(s, "%TZ_M480%", cfg.timezoneOffsetMin == -480 ? "selected" : "");
    replaceAll(s, "%TZ_60%", cfg.timezoneOffsetMin == 60 ? "selected" : "");
    replaceAll(s, "%TZ_120%", cfg.timezoneOffsetMin == 120 ? "selected" : "");
    replaceAll(s, "%TZ_480%", cfg.timezoneOffsetMin == 480 ? "selected" : "");
    replaceAll(s, "%TZ_540%", cfg.timezoneOffsetMin == 540 ? "selected" : "");
    return s;
}

void handleRoot(void*) {
    server().send(200, "text/html", renderHtml());
}

void handleSave(void*) {
    if (server().hasArgument("ssid")) {
        const auto ssid = server().argument("ssid"), pass = server().argument("pass");
        WatchConfig::setWifi(ssid.c_str(), pass.c_str());
    }
    if (server().hasArgument("dav_srv")) {
        const auto srv = server().argument("dav_srv"), usr = server().argument("dav_usr");
        const auto pwd = server().argument("dav_pwd"), cal = server().argument("dav_cal");
        const auto todo = server().argument("dav_tod");
        WatchConfig::setCalDav(srv.c_str(), usr.c_str(), pwd.c_str(), cal.c_str(), todo.c_str());
    }
    if (server().hasArgument("tz")) {
        const auto timezone = server().argument("tz");
        WatchConfig::setTimezone(static_cast<int16_t>(strtol(timezone.c_str(), nullptr, 10)));
    }
    if (server().hasArgument("fmt_24h")) {
        WatchConfig::setTimeFormat(server().argument("fmt_24h") == "1");
    }
    WatchConfig::save();

    const bool syncNow = (server().argument("sync_ntp") == "1");
    std::string msg = "<html><body style='font-family:sans-serif;text-align:center;padding:40px;'>";
    msg += "<h2>Settings Saved!</h2>";
    if (syncNow) {
        msg += "<p>Hotspot shutting down to sync NTP time with router...</p>";
    } else {
        msg += "<p>Configuration stored to flash.</p>";
    }
    msg += "<a href='/'>Back</a></body></html>";
    server().send(200, "text/html", msg);

    if (syncNow) {
        ersa::board::Board::current().delayMs(1000);
        AppPortal::stop();
        NetSync::syncNtp();
    }
}

void handleStop(void*) {
    server().send(200, "text/html", "<html><body style='font-family:sans-serif;text-align:center;padding:40px;'><h2>Hotspot Stopped</h2><p>You may close this tab.</p></body></html>");
    ersa::board::Board::current().delayMs(500);
    AppPortal::stop();
}

void startAp() {
    const auto& cfg = WatchConfig::get();
    auto& wifi = ersa::board::Board::current().getWifi();
    const auto result = wifi.startAccessPoint(cfg.apSsid, cfg.apPass[0] != '\0' ? cfg.apPass : nullptr);
    if (result.isError()) {
        DebugLog::log("HOTSPOT failed to start");
        return;
    }
    constexpr uint8_t captiveAddress[4] = {192, 168, 4, 1};
    dnsServer().begin(53, "*", captiveAddress);

    if (!routesInstalled) {
        server().route("/", ersa::hal::HttpMethod::Get, handleRoot, nullptr);
        server().route("/save", ersa::hal::HttpMethod::Post, handleSave, nullptr);
        server().route("/stop", ersa::hal::HttpMethod::Post, handleStop, nullptr);

        // Captive portal probes
        server().route("/generate_204", ersa::hal::HttpMethod::Get, handleRoot, nullptr);
        server().route("/fwlink", ersa::hal::HttpMethod::Get, handleRoot, nullptr);
        server().route("/hotspot-detect.html", ersa::hal::HttpMethod::Get, handleRoot, nullptr);
        server().routeNotFound(handleRoot, nullptr);
        routesInstalled = true;
    }

    server().begin(80);
    apRunning = true;
    apStartTime = ersa::board::Board::current().getUptimeMs();
    char address[24] = {};
    wifi.copyAccessPointAddress(address, sizeof(address));
    DebugLog::log("HOTSPOT started SSID='%s' IP=%s", cfg.apSsid, address);
}

void stopAp() {
    server().stop();
    dnsServer().stop();
    auto& wifi = ersa::board::Board::current().getWifi();
    wifi.stopAccessPoint();
    wifi.disconnect(true);
    apRunning = false;
    DebugLog::log("HOTSPOT stopped (power save)");
}
} // namespace

void begin() {
    apRunning = false;
}

bool isActive() {
    return apRunning;
}

void stop() {
    if (apRunning) stopAp();
}

void tick() {
    if (!apRunning) return;

    dnsServer().process();
    server().handleClient();

    // Auto-timeout shutoff to prevent battery drain
    const auto& cfg = WatchConfig::get();
    if (cfg.apTimeoutSec > 0 && uint32_t(ersa::board::Board::current().getUptimeMs() - apStartTime) >= (uint32_t(cfg.apTimeoutSec) * 1000)) {
        DebugLog::log("HOTSPOT auto-off timer expired (%u sec)", cfg.apTimeoutSec);
        stopAp();
    }
}

bool onButton(Buttons::Event event) {
    if (event == Buttons::Event::Action) {
        if (apRunning) {
            stop();
        } else {
            startAp();
        }
        return true;
    } else if (event == Buttons::Event::ActionLong) {
        // Long press triggers direct NTP sync if WiFi is configured!
        stop();
        NetSync::syncNtp();
        return true;
    }
    return false;
}

void render(ersa::hal::IDisplay& display) {
    display.fillScreen(0);   // Solid black
    display.setTextColor(1); // White

    display.setFont(ersa::hal::FontFace::MiSansBold10);
    display.setCursor(18, 24);
    display.print("hotspot");

    display.setFont(ersa::hal::FontFace::MiSansRegular8);

    const auto& cfg = WatchConfig::get();
    constexpr int16_t leftX = 18;
    constexpr int16_t valX = 86;
    constexpr int16_t startY = 48;
    constexpr int16_t rowHeight = 20;

    // Status
    display.setCursor(leftX, startY);
    display.print("state");
    display.setCursor(valX, startY);
    display.print(apRunning ? "active" : "off");

    // SSID
    display.setCursor(leftX, startY + rowHeight);
    display.print("ssid");
    display.setCursor(valX, startY + rowHeight);
    display.print(cfg.apSsid);

    // Password
    display.setCursor(leftX, startY + rowHeight * 2);
    display.print("pass");
    display.setCursor(valX, startY + rowHeight * 2);
    display.print(cfg.apPass[0] != '\0' ? cfg.apPass : "(open)");

    // IP
    display.setCursor(leftX, startY + rowHeight * 3);
    display.print("web ip");
    display.setCursor(valX, startY + rowHeight * 3);
    char address[24] = "192.168.4.1";
    if (apRunning) ersa::board::Board::current().getWifi().copyAccessPointAddress(address, sizeof(address));
    display.print(address);

    if (apRunning) {
        const uint32_t elapsed = (ersa::board::Board::current().getUptimeMs() - apStartTime) / 1000;
        const int32_t remain = int32_t(cfg.apTimeoutSec) - int32_t(elapsed);
        display.setCursor(leftX, startY + rowHeight * 4);
        display.print("timeout");
        display.setCursor(valX, startY + rowHeight * 4);
        if (remain > 0) {
            char tBuf[16];
            snprintf(tBuf, sizeof(tBuf), "%lum %lus", (unsigned long)(remain / 60), (unsigned long)(remain % 60));
            display.print(tBuf);
        } else {
            display.print("closing");
        }

        display.setCursor(leftX, 150);
        display.print("connect phone to configure");

        display.setCursor(leftX, 186);
        display.print("stop AP B2   menu B1");
    } else {
        display.setCursor(leftX, 150);
        display.print("open AP to configure Wi-Fi");

        display.setCursor(leftX, 186);
        display.print("start AP B2   menu B1");
    }
}

} // namespace AppPortal
