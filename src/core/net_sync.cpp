#include "net_sync.h"
#include "watch_config.h"
#include "watch_clock.h"
#include "debug_log.h"
#include "ersa/config/system_defaults.h"
#include "ersa/config/ui_strings.h"
#include "ersa/services/time_service.h"
#include "ersa/board/board.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <time.h>
#include <ctype.h>
#include <atomic>
#include <esp_sntp.h>
#include <esp_heap_caps.h>

namespace NetSync {

namespace {
CalEvent events[MAX_EVENTS];
size_t numEvents = 0;

CalTodo todos[MAX_TODOS];
size_t numTodos = 0;

std::atomic<bool> syncing{false};
std::atomic<bool> bootSyncRequested{false};
std::atomic<bool> bootSyncFinished{false};
std::atomic<bool> bootSyncFailed{false};
std::atomic<bool> bootSyncHttpFallback{false};
std::atomic<uint32_t> pendingBootLocalEpoch{0};
bool tlsAllocationFailed = false;
char statusMsg[48] = "Ready";
Preferences cachePrefs;
constexpr const char* CACHE_NS = ersa::config::PREFS_NS_CACHE;

void safeCopy(char* dest, const char* src, size_t maxLen) {
    if (!dest || maxLen == 0) return;
    if (!src) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, src, maxLen - 1);
    dest[maxLen - 1] = '\0';
}

uint32_t calendarDayKey(uint32_t epoch) {
    const DateTime date(epoch);
    return static_cast<uint32_t>(date.year()) * 10000U +
           static_cast<uint32_t>(date.month()) * 100U + date.day();
}

uint32_t todayKey() {
    const DateTime now = WatchClock::now();
    return static_cast<uint32_t>(now.year()) * 10000U +
           static_cast<uint32_t>(now.month()) * 100U + now.day();
}

size_t countEventsForDay(uint32_t key, size_t limit = MAX_EVENTS) {
    size_t count = 0;
    for (size_t i = 0; i < limit; ++i) {
        if (events[i].dayKey == key) ++count;
    }
    return count;
}

void loadDefaultsIfEmpty() {
    // No placeholder event: keep the agenda genuinely empty until a calendar sync succeeds.
    if (numTodos == 0) {
        safeCopy(todos[0].title, "Connect to Hotspot", sizeof(todos[0].title));
        todos[0].completed = false;
        safeCopy(todos[1].title, "Configure WiFi", sizeof(todos[1].title));
        todos[1].completed = false;
        safeCopy(todos[2].title, "Sync CalDAV & NTP", sizeof(todos[2].title));
        todos[2].completed = false;
        numTodos = 3;
    }
}

void saveCache() {
    if (cachePrefs.begin(CACHE_NS, false)) {
        cachePrefs.putBytes("events", events, sizeof(events));
        cachePrefs.putUChar("ev_cnt", (uint8_t)numEvents);
        cachePrefs.putUChar("ev_ver", 2);
        cachePrefs.putBytes("todos", todos, sizeof(todos));
        cachePrefs.putUChar("td_cnt", (uint8_t)numTodos);
        cachePrefs.end();
    }
}

void loadCache() {
    bool hasInitializedCache = false;
    if (cachePrefs.begin(CACHE_NS, true)) {
        hasInitializedCache = cachePrefs.isKey("ev_cnt");
        if (hasInitializedCache) {
            const size_t cachedBytes = cachePrefs.getBytesLength("events");
            const bool currentFormat = cachePrefs.getUChar("ev_ver", 0) == 2 &&
                                       cachedBytes == sizeof(events);
            if (currentFormat) {
                numEvents = cachePrefs.getUChar("ev_cnt", 0);
                if (numEvents > MAX_EVENTS) numEvents = 0;
                if (numEvents > 0) {
                    cachePrefs.getBytes("events", events, sizeof(events));
                }
            } else {
                numEvents = 0;
                DebugLog::log("NET: Calendar cache missing or old format; sync to refresh");
            }

            numTodos = cachePrefs.getUChar("td_cnt", 0);
            if (numTodos > MAX_TODOS) numTodos = 0;
            if (numTodos > 0) {
                cachePrefs.getBytes("todos", todos, sizeof(todos));
            }
        }
        cachePrefs.end();
    }
    // Only load setup guidance if the user has never synced before
    if (!hasInitializedCache) {
        loadDefaultsIfEmpty();
    }
}

bool connectWiFi(const WatchConfig::Config& cfg, bool updateStatus = true) {
    if (cfg.wifiSsid[0] == '\0') {
        if (updateStatus) safeCopy(statusMsg, ersa::strings::MSG_WIFI_NO_SSID, sizeof(statusMsg));
        DebugLog::log("NET: WiFi SSID empty; configure via Hotspot");
        return false;
    }

    if (updateStatus) safeCopy(statusMsg, ersa::strings::MSG_WIFI_CONNECTING, sizeof(statusMsg));
    DebugLog::log("NET: Connecting to '%s'", cfg.wifiSsid);
    // Wi-Fi and BLE coexistence on this ESP32-C3 requires STA modem sleep.
    // Disabling it while the BLE controller is enabled triggers an IDF abort.
    auto& wifi = ersa::board::Board::current().getWifi();
    wifi.connectStation(cfg.wifiSsid, cfg.wifiPass, true);

    const uint32_t startMs = millis();
    while (wifi.state() != ersa::hal::WifiState::Connected &&
           (millis() - startMs) < ersa::config::WIFI_CONNECT_TIMEOUT_MS) {
        delay(200);
    }

    if (wifi.state() != ersa::hal::WifiState::Connected) {
        if (updateStatus) safeCopy(statusMsg, ersa::strings::MSG_WIFI_FAILED, sizeof(statusMsg));
        DebugLog::log("NET: WiFi connect timeout");
        wifi.disconnect(true);
        return false;
    }

    char address[24] = {};
    wifi.copyLocalAddress(address, sizeof(address));
    DebugLog::log("NET: WiFi connected, IP=%s", address);
    return true;
}

void disconnectWiFi() {
    ersa::board::Board::current().getWifi().disconnect(true);
    DebugLog::log("NET: WiFi turned off (power save)");
}

String buildCalDavUrl(const WatchConfig::Config& cfg, const char* calendarName,
                      bool encodeAt = false, bool tasksOnly = false) {
    String s = String(cfg.caldavServer);
    s.trim();
    if (s.isEmpty()) return "";

    // Support webcal:// URLs by converting to https://
    if (s.startsWith("webcal://")) {
        s = "https://" + s.substring(9);
    }

    // Direct ICS file links do not support SabreDAV export filters.
    if (s.endsWith(".ics") || s.indexOf(".ics?") != -1) {
        return s;
    }

    while (s.endsWith("/")) s.remove(s.length() - 1);
    const int queryStart = s.indexOf('?');
    if (queryStart >= 0) s.remove(queryStart);

    const bool calendarEndpoint = s.indexOf("/calendars/") != -1;

    if (s.indexOf("/remote.php/dav") == -1) {
        s += "/remote.php/dav";
    }

    if (!calendarEndpoint && cfg.caldavUser[0] != '\0') {
        s += "/calendars/";
        String u = String(cfg.caldavUser);
        if (encodeAt) u.replace("@", "%40");
        s += u;
        s += "/";
        String c = (calendarName && calendarName[0] != '\0') ? String(calendarName) : String("personal");
        if (encodeAt) c.replace("@", "%40");
        s += c;
        // SabreDAV's export endpoint can filter event exports by time range.
        // Query timestamps are UTC; WatchClock keeps local wall time as epoch.
        const DateTime now = WatchClock::now();
        const DateTime localDayStart(now.year(), now.month(), now.day(), 0, 0, 0);
        const int64_t utcDayStart = static_cast<int64_t>(localDayStart.unixtime()) -
                                    static_cast<int64_t>(cfg.timezoneOffsetMin) * 60;
        const int64_t start = utcDayStart - 3LL * 86400LL;
        const int64_t end = utcDayStart + 4LL * 86400LL;
        s += "?export";
        if (tasksOnly) {
            s += "&componentType=VTODO";
        } else {
            s += "&start=" + String(static_cast<unsigned long>(start));
            s += "&end=" + String(static_cast<unsigned long>(end));
            s += "&expand=1";
        }
    }

    // Config may contain a complete calendar collection URL rather than the
    // DAV root. Give it the same bounded event export / task-only behavior.
    if (calendarEndpoint) {
        const DateTime now = WatchClock::now();
        const DateTime localDayStart(now.year(), now.month(), now.day(), 0, 0, 0);
        const int64_t utcDayStart = static_cast<int64_t>(localDayStart.unixtime()) -
                                    static_cast<int64_t>(cfg.timezoneOffsetMin) * 60;
        s += "?export";
        if (tasksOnly) {
            s += "&componentType=VTODO";
        } else {
            s += "&start=" + String(static_cast<unsigned long>(utcDayStart - 3LL * 86400LL));
            s += "&end=" + String(static_cast<unsigned long>(utcDayStart + 4LL * 86400LL));
            s += "&expand=1";
        }
    }

    return s;
}

uint32_t parseIcsDateTimeToEpoch(const char* dt, int tzOffsetMin) {
    if (!dt || strlen(dt) < 8) return 0;
    char yBuf[5] = {dt[0], dt[1], dt[2], dt[3], '\0'};
    char mBuf[3] = {dt[4], dt[5], '\0'};
    char dBuf[3] = {dt[6], dt[7], '\0'};
    uint16_t y = atoi(yBuf);
    uint8_t m = atoi(mBuf);
    uint8_t d = atoi(dBuf);
    uint8_t h = 0, min = 0, s = 0;
    bool isUtc = false;

    const char* t = strchr(dt, 'T');
    if (t && strlen(t) >= 5) {
        char hBuf[3] = {t[1], t[2], '\0'};
        char minBuf[3] = {t[3], t[4], '\0'};
        h = atoi(hBuf);
        min = atoi(minBuf);
        if (strlen(t) >= 7 && isdigit((unsigned char)t[5]) && isdigit((unsigned char)t[6])) {
            char sBuf[3] = {t[5], t[6], '\0'};
            s = atoi(sBuf);
        }
        if (strchr(t, 'Z')) isUtc = true;
    }

    if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) return 0;

    DateTime dtObj(y, m, d, h, min, s);
    uint32_t epoch = dtObj.unixtime();
    if (isUtc) {
        epoch = static_cast<uint32_t>((int64_t)epoch + ((int64_t)tzOffsetMin * 60));
    }
    return epoch;
}

static String s_caldavCookie = "";

bool fetchAndParseIcs(WiFiClientSecure& client, HTTPClient& https, const String& url,
                      const WatchConfig::Config& cfg,
                      size_t& outEvents, size_t& outTodos) {
    if (url.isEmpty()) return false;
    DebugLog::log("NET: Fetching ICS heap=%lu largest=%lu",
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));

    client.setTimeout(ersa::config::CALDAV_HTTP_TIMEOUT_MS / 1000);
    if (!https.begin(client, url)) {
        DebugLog::log("NET: HTTPClient begin failed");
        return false;
    }

    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (cfg.caldavUser[0] != '\0' && cfg.caldavPass[0] != '\0') {
        https.setAuthorization(cfg.caldavUser, cfg.caldavPass);
    }
    https.setTimeout(ersa::config::CALDAV_HTTP_TIMEOUT_MS);

    // Essential Nextcloud / SabreDAV API headers to bypass CSRF strict cookie check
    https.addHeader("OCS-APIRequest", "true");
    https.addHeader("X-Requested-With", "XMLHttpRequest");
    https.addHeader("User-Agent", "ErsaWearable/1.0 (CalDAV client)");
    https.addHeader("Accept", "text/calendar, text/plain, */*");

    if (s_caldavCookie.length() > 0) {
        https.addHeader("Cookie", s_caldavCookie.c_str());
    }

    const char* headerKeys[] = {"Set-Cookie", "Content-Type"};
    https.collectHeaders(headerKeys, 2);

    int code = https.GET();
    DebugLog::log("NET: ICS GET code=%d", code);

    if (code < 0) {
        char tlsError[96] = {};
        const int tlsErrorCode = client.lastError(tlsError, sizeof(tlsError));
        DebugLog::log("NET: CalDAV transport error=%d TLS=%d (%s) free_heap=%lu",
                      code, tlsErrorCode, tlsError,
                      static_cast<unsigned long>(ESP.getFreeHeap()));
        // mbedTLS -0x7F00 is MBEDTLS_ERR_SSL_ALLOC_FAILED. Repeating the same
        // handshake with URL spelling/task-path fallbacks cannot recover RAM.
        if (tlsErrorCode == -0x7F00 || strstr(tlsError, "CTR_DRBG") != nullptr ||
            strstr(tlsError, "allocation") != nullptr) {
            tlsAllocationFailed = true;
        }
        https.end();
        return false;
    }

    if (https.hasHeader("Set-Cookie")) {
        String sc = https.header("Set-Cookie");
        int semi = sc.indexOf(';');
        s_caldavCookie = (semi != -1) ? sc.substring(0, semi) : sc;
        DebugLog::log("NET: Captured session cookie: %s", s_caldavCookie.c_str());
    }

    // If Nextcloud returned 412 (Strict Cookie missing) and sent a cookie, retry with the cookie
    if (code == 412 && s_caldavCookie.length() > 0) {
        https.end();
        if (https.begin(client, url)) {
            https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
            if (cfg.caldavUser[0] != '\0' && cfg.caldavPass[0] != '\0') {
                https.setAuthorization(cfg.caldavUser, cfg.caldavPass);
            }
            https.setTimeout(ersa::config::CALDAV_HTTP_TIMEOUT_MS);
            https.addHeader("OCS-APIRequest", "true");
            https.addHeader("X-Requested-With", "XMLHttpRequest");
            https.addHeader("User-Agent", "ErsaWearable/1.0 (CalDAV client)");
            https.addHeader("Accept", "text/calendar, text/plain, */*");
            https.addHeader("Cookie", s_caldavCookie.c_str());
            code = https.GET();
            DebugLog::log("NET: ICS GET retry code=%d", code);
        }
    }

    if (code != 200) {
        String err = https.getString();
        if (err.length() > 0) {
            DebugLog::log("NET: ICS error body: %s", err.substring(0, 100).c_str());
        }
        https.end();
        return false;
    }

    WiFiClient* stream = https.getStreamPtr();
    bool inEvent = false;
    bool inTodo = false;
    char curSummary[48] = "";
    char curDt[24] = "";
    char curDtEnd[24] = "";
    char curRrule[48] = "";
    bool isCompleted = false;

    // Buffer tasks so open/pending tasks are prioritized first
    CalTodo openTodos[MAX_TODOS];
    size_t numOpen = 0;
    CalTodo doneTodos[MAX_TODOS];
    size_t numDone = 0;

    const DateTime now = WatchClock::now();
    DateTime dayStart(now.year(), now.month(), now.day(), 0, 0, 0);
    const uint32_t dayStartSec = dayStart.unixtime();
    const uint32_t cacheStartSec = dayStartSec - 3U * 86400U;
    const uint32_t cacheEndSec = dayStartSec + 4U * 86400U;

    while (https.connected() && stream->available()) {
        String line = stream->readStringUntil('\n');
        line.trim();

        if (line == "BEGIN:VEVENT") {
            inEvent = true;
            inTodo = false;
            curSummary[0] = '\0';
            curDt[0] = '\0';
            curDtEnd[0] = '\0';
            curRrule[0] = '\0';
        } else if (line == "BEGIN:VTODO") {
            inTodo = true;
            inEvent = false;
            curSummary[0] = '\0';
            isCompleted = false;
        } else if (line.startsWith("SUMMARY")) {
            int colon = line.indexOf(':');
            if (colon != -1) {
                safeCopy(curSummary, line.substring(colon + 1).c_str(), sizeof(curSummary));
            }
        } else if (inEvent && line.startsWith("DTSTART")) {
            int colon = line.indexOf(':');
            if (colon != -1) {
                safeCopy(curDt, line.substring(colon + 1).c_str(), sizeof(curDt));
            }
        } else if (inEvent && line.startsWith("DTEND")) {
            int colon = line.indexOf(':');
            if (colon != -1) {
                safeCopy(curDtEnd, line.substring(colon + 1).c_str(), sizeof(curDtEnd));
            }
        } else if (inEvent && line.startsWith("RRULE:")) {
            safeCopy(curRrule, line.c_str(), sizeof(curRrule));
        } else if (inTodo && (line.indexOf("STATUS:COMPLETED") != -1 || line.startsWith("COMPLETED:"))) {
            isCompleted = true;
        } else if (line == "END:VEVENT") {
            if (inEvent && curSummary[0] != '\0' && outEvents < MAX_EVENTS) {
                uint32_t startEpoch = parseIcsDateTimeToEpoch(curDt, cfg.timezoneOffsetMin);
                uint32_t endEpoch = (curDtEnd[0] != '\0') ? parseIcsDateTimeToEpoch(curDtEnd, cfg.timezoneOffsetMin) : (startEpoch + 3600);
                if (endEpoch <= startEpoch) endEpoch = startEpoch + 1800;

                bool isToday = startEpoch < dayStartSec + 86400U && endEpoch > dayStartSec;
                // Keep the existing simple recurrence fallback for servers that
                // do not expand RRULEs; SabreDAV range exports expand them.
                if (!isToday && curRrule[0] != '\0' && startEpoch < dayStartSec + 86400U) {
                    bool expired = false;
                    const char* untilPtr = strstr(curRrule, "UNTIL=");
                    if (untilPtr) {
                        uint32_t untilEpoch = parseIcsDateTimeToEpoch(untilPtr + 6, cfg.timezoneOffsetMin);
                        if (untilEpoch > 0 && untilEpoch < dayStartSec) {
                            expired = true;
                        }
                    }

                    if (!expired) {
                        if (strstr(curRrule, "FREQ=DAILY")) {
                            isToday = true;
                        } else if (strstr(curRrule, "FREQ=WEEKLY")) {
                            static const char* const dowCodes[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
                            const char* todayCode = dowCodes[now.dayOfTheWeek() % 7];
                            const char* byDay = strstr(curRrule, "BYDAY=");
                            if (byDay) {
                                if (strstr(byDay, todayCode)) isToday = true;
                            } else {
                                DateTime origStart(startEpoch);
                                if (origStart.dayOfTheWeek() == now.dayOfTheWeek()) isToday = true;
                            }
                        }
                    }
                }

                for (uint32_t eventDay = cacheStartSec;
                     eventDay < cacheEndSec && outEvents < MAX_EVENTS;
                     eventDay += 86400U) {
                    const bool overlapsDay = startEpoch < eventDay + 86400U &&
                                             endEpoch > eventDay;
                    const bool fallbackToday = eventDay == dayStartSec && isToday;
                    if ((!overlapsDay && !fallbackToday) ||
                        countEventsForDay(calendarDayKey(eventDay), outEvents) >= MAX_EVENTS_PER_DAY) {
                        continue;
                    }

                    safeCopy(events[outEvents].title, curSummary, sizeof(events[0].title));
                    events[outEvents].dayKey = calendarDayKey(eventDay);
                    if (strchr(curDt, 'T')) {
                        DateTime localStart(startEpoch);
                        snprintf(events[outEvents].timeStr, sizeof(events[0].timeStr),
                                 "%02u:%02u", localStart.hour(), localStart.minute());
                    } else {
                        safeCopy(events[outEvents].timeStr, "all day", sizeof(events[0].timeStr));
                    }
                    ++outEvents;
                }
            }
            inEvent = false;
            curSummary[0] = '\0';
            curDt[0] = '\0';
            curDtEnd[0] = '\0';
            curRrule[0] = '\0';
        } else if (line == "END:VTODO") {
            if (inTodo && curSummary[0] != '\0') {
                if (!isCompleted) {
                    if (numOpen < MAX_TODOS) {
                        safeCopy(openTodos[numOpen].title, curSummary, sizeof(openTodos[0].title));
                        openTodos[numOpen].completed = false;
                        ++numOpen;
                    }
                } else {
                    if (numDone < MAX_TODOS) {
                        safeCopy(doneTodos[numDone].title, curSummary, sizeof(doneTodos[0].title));
                        doneTodos[numDone].completed = true;
                        ++numDone;
                    }
                }
            }
            inTodo = false;
            curSummary[0] = '\0';
            isCompleted = false;
        }
    }

    https.end();

    // Fill todos array with open tasks first, then completed tasks if space remains
    outTodos = 0;
    for (size_t i = 0; i < numOpen && outTodos < MAX_TODOS; ++i) {
        todos[outTodos++] = openTodos[i];
    }
    for (size_t i = 0; i < numDone && outTodos < MAX_TODOS; ++i) {
        todos[outTodos++] = doneTodos[i];
    }

    return true;
}

} // namespace

void begin() {
    loadCache();
}

size_t eventCount() { return countEventsForDay(todayKey(), numEvents); }

const CalEvent& getEvent(size_t index) {
    const uint32_t key = todayKey();
    for (size_t i = 0; i < numEvents; ++i) {
        if (events[i].dayKey != key) continue;
        if (index == 0) return events[i];
        --index;
    }
    static const CalEvent emptyEvent{};
    return emptyEvent;
}

size_t todoCount() { return numTodos; }
const CalTodo& getTodo(size_t index) { return todos[index < numTodos ? index : 0]; }

void toggleTodo(size_t index) {
    if (index < numTodos) {
        todos[index].completed = !todos[index].completed;
        saveCache();
        DebugLog::log("TODO [%u] '%s' -> %s", unsigned(index), todos[index].title,
                      todos[index].completed ? "DONE" : "OPEN");
    }
}

bool isSyncing() { return syncing.load(std::memory_order_acquire); }
const char* lastStatus() { return statusMsg; }

time_t parseHttpDateToEpoch(const char* str) {
    if (!str || strlen(str) < 16) return 0;

    // Format: "Mon, 28 Sep 2026 06:21:00 GMT" or "28 Sep 2026 06:21:00 GMT"
    const char* p = strchr(str, ',');
    p = p ? (p + 1) : str;

    while (*p == ' ') p++;
    int day = atoi(p);
    if (day < 1 || day > 31) return 0;

    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    static const char* const months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    int month = 0;
    for (int m = 0; m < 12; ++m) {
        if (strncasecmp(p, months[m], 3) == 0) {
            month = m + 1;
            break;
        }
    }
    if (month == 0) return 0;

    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    int year = atoi(p);
    if (year < 2024 || year > 2099) return 0;

    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    int hour = atoi(p);
    p = strchr(p, ':');
    if (!p) return 0;
    int min = atoi(p + 1);
    p = strchr(p + 1, ':');
    if (!p) return 0;
    int sec = atoi(p + 1);

    DateTime dt(year, month, day, hour, min, sec);
    return dt.unixtime();
}

bool fetchHttpUtc(time_t& outUtc, uint32_t timeoutMs = ersa::config::HTTP_TIME_TIMEOUT_MS) {
    HTTPClient http;
    const char* headerKeys[] = {"Date"};

    for (size_t i = 0; i < ersa::config::NUM_HTTP_TIME_ENDPOINTS; ++i) {
        const char* endpoint = ersa::config::DEFAULT_HTTP_TIME_ENDPOINTS[i];
        DebugLog::log("NET: Trying HTTP time fallback [%u]: %s", unsigned(i), endpoint);

        if (!http.begin(endpoint)) continue;
        http.setTimeout(timeoutMs);
        http.collectHeaders(headerKeys, 1);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

        const int httpCode = http.GET();
        if (httpCode > 0) {
            if (http.hasHeader("Date")) {
                String dateHdr = http.header("Date");
                DebugLog::log("NET: HTTP %s returned code %d, Date: '%s'", endpoint, httpCode, dateHdr.c_str());
                time_t parsedUtc = parseHttpDateToEpoch(dateHdr.c_str());
                if (parsedUtc >= 1700000000) {
                    outUtc = parsedUtc;
                    http.end();
                    return true;
                }
            }

            // Check JSON for unixtime
            if (httpCode == 200 && http.getSize() > 0) {
                String body = http.getString();
                int idx = body.indexOf("\"unixtime\":");
                if (idx != -1) {
                    const char* numPtr = body.c_str() + idx + 11;
                    while (*numPtr == ' ') numPtr++;
                    uint32_t unixTime = strtoul(numPtr, nullptr, 10);
                    if (unixTime >= 1700000000) {
                        outUtc = unixTime;
                        http.end();
                        return true;
                    }
                }
            }
        }
        http.end();
    }
    return false;
}

static volatile bool s_sntpSynced = false;
void sntpTimeSyncNotification(struct timeval* tv) {
    (void)tv;
    s_sntpSynced = true;
    DebugLog::log("NET: SNTP packet received and processed");
}

bool fetchNtpUtc(time_t& outUtc, uint32_t timeoutMs = ersa::config::NTP_SYNC_TIMEOUT_MS) {
    s_sntpSynced = false;
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    for (size_t i = 0; i < ersa::config::NUM_NTP_SERVERS && i < 3; ++i) {
        esp_sntp_setservername(i, ersa::config::DEFAULT_NTP_SERVERS[i]);
        DebugLog::log("NET: Set NTP server[%u] = %s", unsigned(i), ersa::config::DEFAULT_NTP_SERVERS[i]);
    }
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
    sntp_set_time_sync_notification_cb(sntpTimeSyncNotification);
    esp_sntp_init();

    const uint32_t startMs = millis();
    while ((millis() - startMs) < timeoutMs) {
        if (s_sntpSynced || sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            outUtc = time(nullptr);
            if (outUtc >= 1700000000) return true;
        }
        time_t tNow = time(nullptr);
        if (tNow >= 1700000000) {
            outUtc = tNow;
            return true;
        }
        delay(100);
    }
    return false;
}

bool fetchTimeWithFallbacks(time_t& outUtc, bool& isHttpFallback, bool updateStatus = true) {
    isHttpFallback = false;
    if (updateStatus) safeCopy(statusMsg, ersa::strings::MSG_SYNCING_NTP, sizeof(statusMsg));

    DebugLog::log("NET: Step 1: Trying SNTP pool sync (UDP port 123)...");
    if (fetchNtpUtc(outUtc, ersa::config::NTP_SYNC_TIMEOUT_MS)) {
        DebugLog::log("NET: Primary SNTP sync SUCCESS (utc=%lu)", (unsigned long)outUtc);
        return true;
    }

    DebugLog::log("NET: SNTP sync timed out/blocked; Step 2: Falling back to HTTP Time endpoints (TCP port 80)...");
    if (fetchHttpUtc(outUtc, ersa::config::HTTP_TIME_TIMEOUT_MS)) {
        isHttpFallback = true;
        DebugLog::log("NET: HTTP Time fallback SUCCESS (utc=%lu)", (unsigned long)outUtc);
        return true;
    }

    DebugLog::log("NET: All network time synchronization methods failed");
    return false;
}

namespace {
void bootTimeSyncTask(void*) {
    const auto& cfg = WatchConfig::get();
    bool success = false;
    bool usedHttp = false;
    time_t utcEpoch = 0;

    // Two bounded attempts cover transient Wi-Fi/NTP startup failures without
    // delaying the display or starving BLE callbacks.
    for (unsigned attempt = 0; attempt < 2 && !success; ++attempt) {
        if (connectWiFi(cfg, false)) {
            success = fetchTimeWithFallbacks(utcEpoch, usedHttp, false);
            disconnectWiFi();
        }
        if (!success && attempt == 0) vTaskDelay(pdMS_TO_TICKS(30000));
    }

    if (success) {
        const int64_t localEpoch = int64_t(utcEpoch) + int64_t(cfg.timezoneOffsetMin) * 60;
        if (localEpoch >= 1704067200LL && localEpoch < 4102444800LL) {
            pendingBootLocalEpoch.store(static_cast<uint32_t>(localEpoch), std::memory_order_release);
            bootSyncHttpFallback.store(usedHttp, std::memory_order_release);
        } else {
            success = false;
        }
    }

    bootSyncFailed.store(!success, std::memory_order_release);
    bootSyncFinished.store(true, std::memory_order_release);
    vTaskDelete(nullptr);
}
} // namespace

bool startBootTimeSync() {
    if (bootSyncRequested.exchange(true, std::memory_order_acq_rel)) return true;
    const auto& cfg = WatchConfig::get();
    if (cfg.wifiSsid[0] == '\0') {
        DebugLog::log("NET: boot time fallback skipped; Wi-Fi is not configured");
        bootSyncFailed.store(true, std::memory_order_release);
        bootSyncFinished.store(true, std::memory_order_release);
        return true;
    }

    bool expected = false;
    if (!syncing.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        bootSyncRequested.store(false, std::memory_order_release);
        return false;
    }
    safeCopy(statusMsg, ersa::strings::MSG_SYNCING_NTP, sizeof(statusMsg));
    bootSyncFinished.store(false, std::memory_order_release);
    bootSyncFailed.store(false, std::memory_order_release);
    pendingBootLocalEpoch.store(0, std::memory_order_release);
    DebugLog::log("NET: boot time fallback scheduled (two bounded Wi-Fi/NTP attempts)");
    if (xTaskCreate(bootTimeSyncTask, "time_sync", 6144, nullptr, 1, nullptr) != pdPASS) {
        syncing.store(false, std::memory_order_release);
        bootSyncRequested.store(false, std::memory_order_release);
        DebugLog::log("NET: could not start boot time fallback task");
        return false;
    }
    return true;
}

void tick() {
    if (!bootSyncFinished.exchange(false, std::memory_order_acq_rel)) return;
    const uint32_t localEpoch = pendingBootLocalEpoch.exchange(0, std::memory_order_acq_rel);
    if (localEpoch) {
        const bool applied = ersa::services::TimeService::instance().submitTime(
            ersa::events::TimeSource::Network, localEpoch);
        const bool usedHttp = bootSyncHttpFallback.load(std::memory_order_acquire);
        if (applied) {
            safeCopy(statusMsg, usedHttp ? ersa::strings::MSG_HTTP_TIME_SYNCED : ersa::strings::MSG_NTP_SYNCED,
                     sizeof(statusMsg));
            DebugLog::log("NET: boot time fallback applied source=%s local=%lu",
                          usedHttp ? "HTTP" : "SNTP", (unsigned long)localEpoch);
        } else {
            DebugLog::log("NET: boot network time ignored; higher-priority source already set the clock");
        }
    } else if (bootSyncFailed.load(std::memory_order_acquire)) {
        safeCopy(statusMsg, ersa::strings::MSG_TIME_SYNC_FAILED, sizeof(statusMsg));
        DebugLog::log("NET: boot time fallback failed; keeping RTC/phone time");
    }
    syncing.store(false, std::memory_order_release);
}

bool syncNtp() {
    const auto& cfg = WatchConfig::get();
    bool expected = false;
    if (!syncing.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return false;

    if (!connectWiFi(cfg)) {
        syncing = false;
        return false;
    }

    time_t utcEpoch = 0;
    bool isHttpFallback = false;
    bool success = fetchTimeWithFallbacks(utcEpoch, isHttpFallback);
    if (success) {
        const uint32_t localEpoch = static_cast<uint32_t>((int64_t)utcEpoch + ((int64_t)cfg.timezoneOffsetMin * 60));
        const bool applied = ersa::services::TimeService::instance().submitTime(
            ersa::events::TimeSource::Network, localEpoch);
        safeCopy(statusMsg, isHttpFallback ? ersa::strings::MSG_HTTP_TIME_SYNCED : ersa::strings::MSG_NTP_SYNCED, sizeof(statusMsg));
        DebugLog::log("NET: Time sync SUCCESS (method=%s, utc=%lu, local=%lu, tzOffset=%d min)",
                      isHttpFallback ? "HTTP" : "SNTP",
                      (unsigned long)utcEpoch, (unsigned long)localEpoch, cfg.timezoneOffsetMin);
        if (!applied) DebugLog::log("NET: network time ignored; higher-priority source already set the clock");
    } else {
        safeCopy(statusMsg, ersa::strings::MSG_TIME_SYNC_FAILED, sizeof(statusMsg));
        DebugLog::log("NET: Time sync failed");
    }

    disconnectWiFi();
    syncing = false;
    return success;
}

bool syncAll() {
    const auto& cfg = WatchConfig::get();
    bool expected = false;
    if (!syncing.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return false;
    tlsAllocationFailed = false;

    if (!connectWiFi(cfg)) {
        syncing = false;
        return false;
    }

    // 1. Sync time with multi-tier fallbacks
    time_t utcEpoch = 0;
    bool isHttpFallback = false;
    if (fetchTimeWithFallbacks(utcEpoch, isHttpFallback)) {
        const uint32_t localEpoch = static_cast<uint32_t>((int64_t)utcEpoch + ((int64_t)cfg.timezoneOffsetMin * 60));
        const bool applied = ersa::services::TimeService::instance().submitTime(
            ersa::events::TimeSource::Network, localEpoch);
        DebugLog::log("NET: Time synced (method=%s, utc=%lu, local=%lu, tzOffset=%d min)",
                      isHttpFallback ? "HTTP" : "SNTP",
                      (unsigned long)utcEpoch, (unsigned long)localEpoch, cfg.timezoneOffsetMin);
        if (!applied) DebugLog::log("NET: network time ignored; higher-priority source already set the clock");
    } else {
        DebugLog::log("NET: Time sync failed in syncAll; keeping RTC time");
    }

    // 2. Sync CalDAV (if server URL configured)
    if (cfg.caldavServer[0] != '\0') {
        safeCopy(statusMsg, ersa::strings::MSG_QUERYING_CALDAV, sizeof(statusMsg));
        DebugLog::log("NET: Querying CalDAV (srv='%s', user='%s', cal='%s', todo='%s')",
                      cfg.caldavServer, cfg.caldavUser, cfg.caldavCalendar, cfg.caldavTodoPath);

        WiFiClientSecure client;
        client.setInsecure(); // Skip TLS cert validation to save flash/RAM
        HTTPClient https;

        size_t parsedEvents = 0;
        size_t parsedTodos = 0;

        // Step A: Fetch events calendar
        String eventsUrl = buildCalDavUrl(cfg, cfg.caldavCalendar, false, false);
        bool ok = fetchAndParseIcs(client, https, eventsUrl, cfg, parsedEvents, parsedTodos);
        if (!ok && !tlsAllocationFailed && eventsUrl.indexOf("@") != -1) {
            String retryUrl = buildCalDavUrl(cfg, cfg.caldavCalendar, true, false);
            ok = fetchAndParseIcs(client, https, retryUrl, cfg, parsedEvents, parsedTodos);
        }

        // Step B: Fetch tasks calendar if different from events calendar
        bool okTasks = false;
        if (!tlsAllocationFailed && cfg.caldavTodoPath[0] != '\0') {
            String tasksUrl = buildCalDavUrl(cfg, cfg.caldavTodoPath, false, true);
            size_t extraEvents = 0;
            size_t extraTodos = parsedTodos;
            okTasks = fetchAndParseIcs(client, https, tasksUrl, cfg, extraEvents, extraTodos);
            if (!okTasks && !tlsAllocationFailed && tasksUrl.indexOf("@") != -1) {
                String retryTasksUrl = buildCalDavUrl(cfg, cfg.caldavTodoPath, true, true);
                okTasks = fetchAndParseIcs(client, https, retryTasksUrl, cfg, extraEvents, extraTodos);
            }
            // If configured tasks path returned 404 or failed, fallback to standard Nextcloud "personal" calendar
            if (!okTasks && !tlsAllocationFailed &&
                strcmp(cfg.caldavTodoPath, ersa::config::FALLBACK_CALDAV_TODO) != 0) {
                DebugLog::log("NET: Tasks at '%s' failed/404; trying fallback '%s'",
                              cfg.caldavTodoPath, ersa::config::FALLBACK_CALDAV_TODO);
                String fallbackTasksUrl = buildCalDavUrl(cfg, ersa::config::FALLBACK_CALDAV_TODO, false, true);
                okTasks = fetchAndParseIcs(client, https, fallbackTasksUrl, cfg, extraEvents, extraTodos);
                if (!okTasks && !tlsAllocationFailed && fallbackTasksUrl.indexOf("@") != -1) {
                    String retryFallback = buildCalDavUrl(cfg, ersa::config::FALLBACK_CALDAV_TODO, true, true);
                    okTasks = fetchAndParseIcs(client, https, retryFallback, cfg, extraEvents, extraTodos);
                }
            }
            if (okTasks) {
                parsedTodos = extraTodos;
            }
        }

        if (ok || okTasks || parsedEvents > 0 || parsedTodos > 0) {
            // Keep the last on-device snapshot for any calendar component
            // whose network request failed; a partial sync must not erase it.
            if (ok) numEvents = parsedEvents;
            if (okTasks) numTodos = parsedTodos;
            saveCache();
            const size_t todaysEvents = eventCount();
            snprintf(statusMsg, sizeof(statusMsg), "%u today, %u cached",
                     (unsigned)todaysEvents, (unsigned)numEvents);
            DebugLog::log("NET: CalDAV sync SUCCESS: today=%u cached=%u todos=%u",
                          (unsigned)todaysEvents, (unsigned)numEvents, (unsigned)numTodos);
        } else if (tlsAllocationFailed) {
            safeCopy(statusMsg, "CalDAV skipped: low memory", sizeof(statusMsg));
            DebugLog::log("NET: CalDAV sync stopped after TLS allocation failure; skipped remaining URL retries");
        } else {
            safeCopy(statusMsg, ersa::strings::MSG_CALDAV_FAILED, sizeof(statusMsg));
            DebugLog::log("NET: CalDAV HTTP request failed");
        }
    } else {
        safeCopy(statusMsg, ersa::strings::MSG_SYNC_COMPLETE, sizeof(statusMsg));
    }

    disconnectWiFi();
    syncing = false;
    return true;
}

} // namespace NetSync
