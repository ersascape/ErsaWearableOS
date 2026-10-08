#include "core/usb_control.h"
#include "core/debug_log.h"
#include "core/watch_clock.h"
#include "ersa/app/application_manager.h"
#include "ersa/services/bluetooth_manager.h"
#include "ersa/services/power_manager.h"
#include "ersa/board/board.h"
#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_image_format.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
constexpr size_t REQUEST_LIMIT = 512;
// Four diagnostic records can exceed the request-sized frame limit. Keep the
// response large enough for a complete batch so sendReply never drops logs.
constexpr size_t RESPONSE_LIMIT = 2048;
char requestBuffer[REQUEST_LIMIT + 1];
size_t requestLength = 0;
bool droppingLongRequest = false;
bool initialized = false;
char responseBuffer[RESPONSE_LIMIT];
size_t responseLength = 0;
size_t responseOffset = 0;
bool restartAfterReply = false;
uint32_t restartDeadlineMs = 0;

struct Slice { const char* data; size_t size; };

void skipSpace(const char*& p) {
    while (*p == ' ' || *p == '\t' || *p == '\r') ++p;
}

bool parseString(const char*& p, Slice* contents = nullptr) {
    if (*p != '"') return false;
    const char* start = ++p;
    while (*p) {
        if (*p == '\\') {
            ++p;
            if (!*p) return false;
            ++p;
        } else if (*p == '"') {
            if (contents) { contents->data = start; contents->size = size_t(p - start); }
            ++p;
            return true;
        } else {
            ++p;
        }
    }
    return false;
}

bool parseValue(const char*& p, Slice& value) {
    const char* start = p;
    bool quoted = false;
    bool escaped = false;
    unsigned depth = 0;
    for (; *p; ++p) {
        const char c = *p;
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
            continue;
        }
        if (c == '"') { quoted = true; continue; }
        if (c == '{' || c == '[') { ++depth; continue; }
        if (c == '}' || c == ']') {
            if (!depth) break;
            --depth;
            continue;
        }
        if (c == ',' && !depth) break;
    }
    if (quoted || depth) return false;
    const char* end = p;
    while (end > start && isspace(static_cast<unsigned char>(end[-1]))) --end;
    while (start < end && isspace(static_cast<unsigned char>(*start))) ++start;
    if (start == end) return false;
    value = {start, size_t(end - start)};
    return true;
}

bool equalSlice(const Slice& value, const char* expected) {
    return strlen(expected) == value.size && memcmp(value.data, expected, value.size) == 0;
}

bool parseUnsigned(const Slice& value, uint32_t& out) {
    if (!value.size) return false;
    uint32_t result = 0;
    for (size_t i = 0; i < value.size; ++i) {
        const char c = value.data[i];
        if (c < '0' || c > '9') return false;
        const uint32_t digit = uint32_t(c - '0');
        if (result > (UINT32_MAX - digit) / 10) return false;
        result = result * 10 + digit;
    }
    out = result;
    return true;
}

bool parseRequest(const char* json, uint32_t& id, uint32_t& version, char* command, size_t commandSize) {
    const char* p = json;
    skipSpace(p);
    if (*p++ != '{') return false;
    bool gotId = false, gotVersion = false, gotCommand = false;
    for (;;) {
        skipSpace(p);
        if (*p == '}') { ++p; break; }
        Slice key{};
        if (!parseString(p, &key)) return false;
        skipSpace(p);
        if (*p++ != ':') return false;
        skipSpace(p);
        Slice value{};
        if (!parseValue(p, value)) return false;
        if (equalSlice(key, "id")) {
            if (gotId || !parseUnsigned(value, id)) return false;
            gotId = true;
        } else if (equalSlice(key, "v")) {
            if (gotVersion || !parseUnsigned(value, version)) return false;
            gotVersion = true;
        } else if (equalSlice(key, "cmd")) {
            if (gotCommand || value.size < 2 || value.data[0] != '"' || value.data[value.size - 1] != '"') return false;
            const size_t n = value.size - 2;
            if (!n || n >= commandSize || memchr(value.data + 1, '\\', n)) return false;
            memcpy(command, value.data + 1, n);
            command[n] = '\0';
            gotCommand = true;
        }
        skipSpace(p);
        if (*p == ',') { ++p; continue; }
        if (*p == '}') { ++p; break; }
        return false;
    }
    skipSpace(p);
    return !*p && gotId && gotVersion && gotCommand;
}

uint32_t requestCursor(const char* json) {
    const char* key = strstr(json, "\"cursor\"");
    if (!key) return UINT32_MAX;
    key += 8;
    while (*key && (*key == ' ' || *key == '\t')) ++key;
    if (*key++ != ':') return UINT32_MAX;
    while (*key == ' ' || *key == '\t') ++key;
    const char* end = key;
    while (*end >= '0' && *end <= '9') ++end;
    if (end == key || size_t(end - key) > 10) return UINT32_MAX;
    Slice value{key, size_t(end - key)};
    uint32_t cursor;
    return parseUnsigned(value, cursor) ? cursor : UINT32_MAX;
}

bool requestUnsignedArg(const char* json, const char* name, uint32_t& out) {
    char key[64];
    const int keyLength = snprintf(key, sizeof(key), "\"%s\"", name);
    if (keyLength <= 0 || size_t(keyLength) >= sizeof(key)) return false;
    const char* p = strstr(json, key);
    if (!p) return false;
    p += keyLength;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p++ != ':') return false;
    while (*p == ' ' || *p == '\t') ++p;
    const char* end = p;
    while (*end >= '0' && *end <= '9') ++end;
    if (end == p) return false;
    Slice value{p, size_t(end - p)};
    return parseUnsigned(value, out);
}

size_t appendEscaped(char* out, size_t cap, size_t pos, const char* text) {
    if (!cap || pos >= cap) return pos;
    out[pos++] = '"';
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        const size_t needed = (*p < 0x20) ? 6 : ((*p == '"' || *p == '\\') ? 2 : 1);
        if (pos + needed + 2 > cap) break; // Reserve closing quote and NUL.
        if (*p == '"' || *p == '\\') { out[pos++] = '\\'; out[pos++] = char(*p); }
        else if (*p < 0x20) {
            const int n = snprintf(out + pos, cap - pos, "\\u%04x", unsigned(*p));
            if (n != 6) break;
            pos += size_t(n);
        } else out[pos++] = char(*p);
    }
    out[pos++] = '"';
    out[pos] = '\0';
    return pos;
}

void sendReply(uint32_t id, bool ok, const char* code, const char* message, const char* data) {
    int n;
    if (responseOffset < responseLength) return;
    if (ok) n = snprintf(responseBuffer, sizeof(responseBuffer), "{\"v\":1,\"id\":%lu,\"ok\":true,\"data\":%s}\n",
                         static_cast<unsigned long>(id), data ? data : "{}");
    else n = snprintf(responseBuffer, sizeof(responseBuffer), "{\"v\":1,\"id\":%lu,\"ok\":false,\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}\n",
                      static_cast<unsigned long>(id), code ? code : "error", message ? message : "request failed");
    if (n <= 0) return;
    if (size_t(n) >= sizeof(responseBuffer)) {
        n = snprintf(responseBuffer, sizeof(responseBuffer),
                     "{\"v\":1,\"id\":%lu,\"ok\":false,\"error\":{\"code\":\"response_too_large\",\"message\":\"reply exceeded the USB frame limit\"}}\n",
                     static_cast<unsigned long>(id));
        if (n <= 0 || size_t(n) >= sizeof(responseBuffer)) return;
    }
    responseLength = size_t(n);
    responseOffset = 0;
}

void flushReply() {
    if (responseOffset >= responseLength) {
        responseOffset = responseLength = 0;
        return;
    }
    auto& console = ersa::board::Board::current().getConsole();
    const size_t writable = console.availableForWrite();
    if (!writable) return;
    const size_t remaining = responseLength - responseOffset;
    const size_t chunk = remaining < writable ? remaining : writable;
    const size_t sent = console.write(reinterpret_cast<const uint8_t*>(responseBuffer + responseOffset), chunk);
    responseOffset += sent;
    if (responseOffset >= responseLength) responseOffset = responseLength = 0;
}

const char* powerStateName(ersa::services::PowerState state) {
    using ersa::services::PowerState;
    switch (state) {
        case PowerState::Active: return "active";
        case PowerState::Idle: return "idle";
        case PowerState::LightSleep: return "light_sleep";
        case PowerState::DeepSleep: return "deep_sleep";
        default: return "unknown";
    }
}

const char* otaImageStateName(esp_ota_img_states_t state) {
    switch (state) {
        case ESP_OTA_IMG_NEW: return "new";
        case ESP_OTA_IMG_PENDING_VERIFY: return "pending_verify";
        case ESP_OTA_IMG_VALID: return "valid";
        case ESP_OTA_IMG_INVALID: return "invalid";
        case ESP_OTA_IMG_ABORTED: return "aborted";
        default: return "undefined";
    }
}

const char* otaSlotName(const esp_partition_t* partition) {
    if (!partition) return "none";
    if (partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) return "ota_0";
    if (partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) return "ota_1";
    return "other";
}

bool otaPartitionBootable(const esp_partition_t* partition) {
    if (!partition) return false;
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(partition, &state) == ESP_OK &&
        (state == ESP_OTA_IMG_INVALID || state == ESP_OTA_IMG_ABORTED)) return false;
    esp_image_header_t header{};
    if (esp_partition_read(partition, 0, &header, sizeof(header)) != ESP_OK ||
        header.magic != ESP_IMAGE_HEADER_MAGIC || header.segment_count == 0 ||
        header.segment_count > ESP_IMAGE_MAX_SEGMENTS) return false;
    size_t offset = sizeof(header);
    for (uint8_t i = 0; i < header.segment_count; ++i) {
        if (offset > partition->size || sizeof(esp_image_segment_header_t) > partition->size - offset)
            return false;
        esp_image_segment_header_t segment{};
        if (esp_partition_read(partition, offset, &segment, sizeof(segment)) != ESP_OK ||
            segment.data_len == 0 || segment.data_len > partition->size - offset - sizeof(segment))
            return false;
        offset += sizeof(segment) + segment.data_len;
    }
    const esp_partition_pos_t position{partition->address, partition->size};
    esp_image_metadata_t metadata{};
    return esp_image_verify(ESP_IMAGE_VERIFY, &position, &metadata) == ESP_OK;
}

void handleRequest(uint32_t id, uint32_t version, const char* command, const char* request) {
    if (version != 1) { sendReply(id, false, "version", "unsupported protocol version", nullptr); return; }
    auto& power = ersa::services::PowerManager::instance();
    auto& bluetooth = ersa::services::BluetoothManager::instance();
    if (strcmp(command, "system.status") == 0) {
        const auto* app = ersa::app::ApplicationManager::instance().getActiveApp();
        const char* appId = app ? app->getId() : "none";
        const auto& device = ersa::board::Board::current().getDeviceInfo();
        const esp_app_desc_t* appDescription = esp_ota_get_app_description();
        char data[RESPONSE_LIMIT];
        snprintf(data, sizeof(data), "{\"firmware\":\"ErsaWearable\",\"version\":\"%s\",\"device_name\":\"%s\",\"codename\":\"%s\",\"manufacturer\":\"%s\",\"build\":\"%s %s\",\"running_slot\":\"%s\",\"uptime_seconds\":%lu,\"reset_reason\":\"%s\",\"active_app\":\"%s\",\"free_heap\":%lu,\"usb_session\":true,\"power_state\":\"%s\",\"power_locks_clear\":%s}",
                 appDescription ? appDescription->version : "unknown",
                 device.name, device.codename, device.manufacturer,
                 __DATE__, __TIME__, otaSlotName(esp_ota_get_running_partition()), static_cast<unsigned long>(millis() / 1000), DebugLog::resetReasonName(), appId,
                 static_cast<unsigned long>(ESP.getFreeHeap()), powerStateName(power.getState()), power.canSleep() ? "true" : "false");
        sendReply(id, true, nullptr, nullptr, data);
    } else if (strcmp(command, "battery.read") == 0) {
        char data[160];
        snprintf(data, sizeof(data), "{\"available\":%s,\"millivolts\":%u,\"percent\":%u,\"sample_age_ms\":%lu,\"connected\":%s,\"charging\":%s}",
                 power.hasBatterySample() ? "true" : "false", unsigned(power.getBatteryMv()), unsigned(power.getBatteryPercent()),
                 static_cast<unsigned long>(power.getBatterySampleAgeMs(millis())),
                 power.isBatteryConnected() ? "true" : "false", power.isCharging() ? "true" : "false");
        sendReply(id, true, nullptr, nullptr, data);
    } else if (strcmp(command, "ble.status") == 0) {
        const auto caps = bluetooth.companionCapabilities();
        char data[256];
        snprintf(data, sizeof(data), "{\"ble_connected\":%s,\"advertising\":%s,\"source\":\"%s\",\"source_available\":%s,\"notifications\":%s,\"media\":%s,\"calls\":%s,\"dial\":%s,\"hangup\":%s}",
                 bluetooth.bleConnected() ? "true" : "false", bluetooth.isAdvertising() ? "true" : "false",
                 bluetooth.companionSourceId(), bluetooth.companionSourceAvailable() ? "true" : "false",
                 caps.notifications ? "true" : "false", caps.media ? "true" : "false", caps.calls ? "true" : "false",
                 caps.dial ? "true" : "false", caps.hangup ? "true" : "false");
        sendReply(id, true, nullptr, nullptr, data);
    } else if (strcmp(command, "ota.status") == 0) {
        const esp_partition_t* running = esp_ota_get_running_partition();
        const esp_partition_t* update = esp_ota_get_next_update_partition(nullptr);
        esp_ota_img_states_t imageState = ESP_OTA_IMG_UNDEFINED;
        const bool stateAvailable = running && esp_ota_get_state_partition(running, &imageState) == ESP_OK;
        const esp_partition_t* slot0 = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
        const esp_partition_t* slot1 = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
        const esp_partition_t* other = running && running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ? slot1 : slot0;
        esp_ota_img_states_t otherState = ESP_OTA_IMG_UNDEFINED;
        const bool otherStateAvailable = other && esp_ota_get_state_partition(other, &otherState) == ESP_OK;
        const bool otherBootable = otaPartitionBootable(other);
        char data[512];
        snprintf(data, sizeof(data),
                 "{\"running_slot\":\"%s\",\"running_offset\":%lu,\"running_size\":%lu,\"image_state\":\"%s\",\"state_available\":%s,\"other_slot\":\"%s\",\"other_image_state\":\"%s\",\"other_state_available\":%s,\"other_bootable\":%s,\"update_slot\":\"%s\",\"update_offset\":%lu,\"update_size\":%lu,\"dual_slot\":%s,\"rollback_enabled\":%s,\"pending_confirmation\":%s}",
                 otaSlotName(running), static_cast<unsigned long>(running ? running->address : 0),
                 static_cast<unsigned long>(running ? running->size : 0),
                 stateAvailable ? otaImageStateName(imageState) : "unavailable", stateAvailable ? "true" : "false",
                 otaSlotName(other), otherStateAvailable ? otaImageStateName(otherState) : "undefined",
                 otherStateAvailable ? "true" : "false", otherBootable ? "true" : "false",
                 otaSlotName(update), static_cast<unsigned long>(update ? update->address : 0),
                 static_cast<unsigned long>(update ? update->size : 0), slot0 && slot1 ? "true" : "false",
#if defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) && CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
                 "true",
#else
                 "false",
#endif
                 stateAvailable && imageState == ESP_OTA_IMG_PENDING_VERIFY ? "true" : "false");
        sendReply(id, true, nullptr, nullptr, data);
    } else if (strcmp(command, "ota.boot-other") == 0) {
        const esp_partition_t* running = esp_ota_get_running_partition();
        const esp_partition_t* slot0 = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
        const esp_partition_t* slot1 = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
        const esp_partition_t* other = running && running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ? slot1 : slot0;
        if (!otaPartitionBootable(other)) {
            sendReply(id, false, "not_bootable", "other OTA slot has no valid boot image", nullptr);
        } else {
            const esp_err_t result = esp_ota_set_boot_partition(other);
            if (result != ESP_OK) {
                sendReply(id, false, "selection_failed", "ESP-IDF rejected the alternate slot", nullptr);
            } else {
                char data[96];
                snprintf(data, sizeof(data), "{\"next_boot_slot\":\"%s\",\"restart_required\":true}", otaSlotName(other));
                sendReply(id, true, nullptr, nullptr, data);
                restartAfterReply = true;
                restartDeadlineMs = 0;
            }
        }
    } else if (strcmp(command, "power.get-dvfs-state") == 0) {
        char report[1400];
        const bool complete = ersa::board::Board::current().getPowerManagement().getPowerModeReport(report, sizeof(report));
        char data[RESPONSE_LIMIT];
        size_t pos = size_t(snprintf(data, sizeof(data), "{\"complete\":%s,\"cpu_mhz\":%u,\"report\":\"",
                                     complete ? "true" : "false", unsigned(getCpuFrequencyMhz())));
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(report);
             *p && pos + 8 < sizeof(data); ++p) {
            if (*p == '"' || *p == '\\') {
                data[pos++] = '\\'; data[pos++] = char(*p);
            } else if (*p == '\n') {
                data[pos++] = '\\'; data[pos++] = 'n';
            } else if (*p == '\r') {
                data[pos++] = '\\'; data[pos++] = 'r';
            } else if (*p >= 0x20) {
                data[pos++] = char(*p);
            }
        }
        if (pos + 3 >= sizeof(data)) { sendReply(id, false, "response_too_large", "DVFS report exceeds control frame", nullptr); return; }
        data[pos++] = '"'; data[pos++] = '}'; data[pos] = '\0';
        sendReply(id, true, nullptr, nullptr, data);
    } else if (strcmp(command, "power.status") == 0) {
        char data[200];
        snprintf(data, sizeof(data), "{\"state\":\"%s\",\"power_locks_clear\":%s,\"cpu_mhz\":%u,\"cpu_test_override_mhz\":%u,\"usb_blocks_sleep\":%s}",
                 powerStateName(power.getState()), power.canSleep() ? "true" : "false", unsigned(getCpuFrequencyMhz()),
                 ersa::board::Board::current().getPowerManagement().testCpuFrequencyMHz(),
                 ersa::board::Board::current().getConsole().isAttached() ? "true" : "false");
        sendReply(id, true, nullptr, nullptr, data);
    } else if (strcmp(command, "power.cpu-freq-get") == 0) {
        char data[128];
        snprintf(data, sizeof(data), "{\"cpu_mhz\":%u,\"test_override_mhz\":%u,\"automatic\":%s}",
                 unsigned(getCpuFrequencyMhz()), ersa::board::Board::current().getPowerManagement().testCpuFrequencyMHz(),
                 ersa::board::Board::current().getPowerManagement().testCpuFrequencyMHz() == 0 ? "true" : "false");
        sendReply(id, true, nullptr, nullptr, data);
    } else if (strcmp(command, "power.cpu-freq-set") == 0) {
        uint32_t mhz = UINT32_MAX;
        if (!requestUnsignedArg(request, "cpu_mhz", mhz) || (mhz != 0 && mhz != 40 && mhz != 80 && mhz != 160)) {
            sendReply(id, false, "invalid_argument", "cpu_mhz must be 0, 40, 80, or 160", nullptr);
        } else if (!ersa::board::Board::current().getPowerManagement().setTestCpuFrequencyMHz(unsigned(mhz))) {
            sendReply(id, false, "unavailable", "DVFS is not initialized or ESP-IDF rejected the test frequency", nullptr);
        } else {
            char data[128];
            snprintf(data, sizeof(data), "{\"requested_profile_mhz\":%u,\"automatic\":%s,\"restart_required\":true}",
                     unsigned(mhz), mhz == 0 ? "true" : "false");
            sendReply(id, true, nullptr, nullptr, data);
            restartAfterReply = true;
            restartDeadlineMs = 0;
        }
    } else if (strcmp(command, "logs.read") == 0) {
        uint32_t limit = 4;
        if (requestUnsignedArg(request, "limit", limit) && (limit < 1 || limit > 4)) {
            sendReply(id, false, "invalid_argument", "limit must be between 1 and 4", nullptr);
            return;
        }
        uint32_t cursor = requestCursor(request);
        if (cursor == UINT32_MAX) {
            const uint32_t latest = DebugLog::latestSequence();
            cursor = latest > limit ? latest - limit : 0;
        }
        DebugLog::Record records[4]{};
        uint32_t observed = cursor;
        const size_t count = DebugLog::readSince(cursor, records, limit, &observed);
        char data[RESPONSE_LIMIT];
        size_t pos = size_t(snprintf(data, sizeof(data), "{\"records\":["));
        uint32_t next = cursor;
        size_t emitted = 0;
        for (size_t i = 0; i < count && pos < sizeof(data); ++i) {
            char prefix[64];
            const int prefixLength = snprintf(prefix, sizeof(prefix),
                                              "%s{\"sequence\":%lu,\"line\":",
                                              emitted ? "," : "",
                                              static_cast<unsigned long>(records[i].sequence));
            if (prefixLength <= 0 || size_t(prefixLength) >= sizeof(prefix) ||
                size_t(prefixLength) + 60 >= sizeof(data) - pos) break;
            memcpy(data + pos, prefix, size_t(prefixLength));
            pos += size_t(prefixLength);
            // Reserve room for the closing object/array, cursor field, and
            // outer NDJSON envelope. Long lines are safely truncated here.
            const size_t reserve = 160;
            if (sizeof(data) - pos <= reserve + 3) break;
            const size_t written = appendEscaped(data + pos, sizeof(data) - pos - reserve, 0, records[i].text);
            if (!written) break;
            pos += written;
            next = records[i].sequence;
            ++emitted;
        }
        snprintf(data + pos, sizeof(data) - pos, "],\"next_cursor\":%lu}", static_cast<unsigned long>(next));
        sendReply(id, true, nullptr, nullptr, data);
    } else if (strcmp(command, "job.get") == 0) {
        sendReply(id, false, "unsupported", "asynchronous jobs are not implemented", nullptr);
    } else {
        sendReply(id, false, "unsupported", "unknown command", nullptr);
    }
}
}

namespace UsbControl {
void begin() {
    requestLength = 0;
    droppingLongRequest = false;
    initialized = true;
}

void tick() {
    if (!initialized) begin();
    auto& console = ersa::board::Board::current().getConsole();
    if (!console.isAttached()) {
        DebugLog::setProtocolMode(false);
        requestLength = 0;
        droppingLongRequest = false;
        responseOffset = responseLength = 0;
        restartAfterReply = false;
        restartDeadlineMs = 0;
        return;
    }
    flushReply();
    if (restartAfterReply && responseLength == 0) {
        if (!restartDeadlineMs) restartDeadlineMs = millis() + 200;
        else if (int32_t(millis() - restartDeadlineMs) >= 0) ESP.restart();
    }
    // Drain the previous response before parsing another request. This keeps
    // replies framed and avoids blocking the watch loop on USB backpressure.
    if (responseLength) return;
    // Bound the work performed in the main loop to preserve UI/BLE latency.
    for (unsigned budget = 0; budget < 64 && console.available(); ++budget) {
        const int incoming = console.read();
        if (incoming < 0) break;
        const char ch = char(incoming);
        if (ch == '\r') continue;
        if (ch != '\n') {
            if (droppingLongRequest) continue;
            if (requestLength == REQUEST_LIMIT) {
                requestLength = 0;
                droppingLongRequest = true;
            } else requestBuffer[requestLength++] = ch;
            continue;
        }
        if (droppingLongRequest) {
            droppingLongRequest = false;
            requestLength = 0;
            continue;
        }
        if (!requestLength) continue;
        requestBuffer[requestLength] = '\0';
        uint32_t id = 0, version = 0;
        char command[48] = {};
        const bool valid = parseRequest(requestBuffer, id, version, command, sizeof(command));
        if (valid) {
            DebugLog::setProtocolMode(true);
            handleRequest(id, version, command, requestBuffer);
        }
        requestLength = 0;
        // One complete request per loop pass, even if the host sent a burst.
        break;
    }
}
}
