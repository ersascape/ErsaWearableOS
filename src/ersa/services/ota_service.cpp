#include "ersa/services/ota_service.h"
#include "ersa/board/board.h"

#include "core/debug_log.h"
#include "core/watch_config.h"
#include "ersa/board/board.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <mbedtls/sha256.h>
#include <cstring>
#include <strings.h>
#include <ctime>
#include <cstdlib>

namespace ersa {
namespace services {
namespace {
constexpr uint32_t BOOT_CONFIRM_DELAY_MS = 30000;
constexpr char OTA_BASE_URL[] = "https://pkgs-wearables.ersa.dev";
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
constexpr size_t MANIFEST_LIMIT = 768;
constexpr int OTA_RANGE_SIZE = 32 * 1024;
constexpr unsigned OTA_RANGE_RETRIES = 4;
constexpr uint32_t OTA_TRANSFER_TIMEOUT_MS = 10 * 60 * 1000;

struct ManifestBuffer {
    char data[MANIFEST_LIMIT]{};
    size_t length{0};
    bool overflow{false};
};

struct OtaManifest {
    char deviceName[48]{};
    char codename[32]{};
    char manufacturer[48]{};
    char tag[32]{};
    char version[32]{};
    char firmwareUrl[192]{};
    char sha256[65]{};
    size_t size{0};
};

esp_err_t logOtaHttpEvent(esp_http_client_event_t* event);

esp_err_t collectManifest(esp_http_client_event_t* event) {
    logOtaHttpEvent(event);
    auto* buffer = static_cast<ManifestBuffer*>(event->user_data);
    if (event->event_id != HTTP_EVENT_ON_DATA || !buffer) return ESP_OK;
    const size_t amount = static_cast<size_t>(event->data_len);
    if (amount >= MANIFEST_LIMIT - buffer->length) {
        buffer->overflow = true;
        return ESP_FAIL;
    }
    memcpy(buffer->data + buffer->length, event->data, amount);
    buffer->length += amount;
    buffer->data[buffer->length] = '\0';
    return ESP_OK;
}

esp_err_t logOtaHttpEvent(esp_http_client_event_t* event) {
    if (!event) return ESP_OK;
    switch (event->event_id) {
        case HTTP_EVENT_ON_CONNECTED:
            DebugLog::log("OTA: HTTPS connection established");
            break;
        case HTTP_EVENT_ON_HEADER:
            if (event->header_key && strcmp(event->header_key, "Status") == 0)
                DebugLog::log("OTA: HTTP response status=%s", event->header_value ? event->header_value : "?");
            break;
        case HTTP_EVENT_DISCONNECTED:
            DebugLog::log("OTA: HTTPS range connection closed");
            break;
        case HTTP_EVENT_ERROR:
            DebugLog::log("OTA: HTTP transport reported an error");
            break;
        default:
            break;
    }
    return ESP_OK;
}

struct RangeResponse {
    uint32_t start{0};
    uint32_t end{0};
    uint32_t total{0};
    bool contentRangeSeen{false};
    bool contentRangeValid{false};
};

esp_err_t rangeHttpEvent(esp_http_client_event_t* event) {
    logOtaHttpEvent(event);
    if (event && event->event_id == HTTP_EVENT_ON_HEADER && event->header_key &&
        strcasecmp(event->header_key, "Content-Range") == 0 && event->header_value) {
        auto* response = static_cast<RangeResponse*>(event->user_data);
        if (response) {
            unsigned long start = 0, end = 0, total = 0;
            response->contentRangeSeen = true;
            response->contentRangeValid = sscanf(event->header_value, "bytes %lu-%lu/%lu",
                                                  &start, &end, &total) == 3 &&
                                           start <= UINT32_MAX && end <= UINT32_MAX &&
                                           total <= UINT32_MAX && start <= end && end < total;
            if (response->contentRangeValid) {
                response->start = static_cast<uint32_t>(start);
                response->end = static_cast<uint32_t>(end);
                response->total = static_cast<uint32_t>(total);
            }
        }
    }
    return ESP_OK;
}

bool jsonString(const char* json, const char* key, char* output, size_t capacity) {
    char pattern[48];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char* value = strstr(json, pattern);
    if (!value) return false;
    value += strlen(pattern);
    const char* end = strchr(value, '"');
    if (!end || size_t(end - value) >= capacity) return false;
    const size_t length = size_t(end - value);
    memcpy(output, value, length);
    output[length] = '\0';
    return true;
}

bool fetchManifest(OtaManifest& manifest) {
    ManifestBuffer body;
    const auto& identity = board::Board::current().getDeviceInfo();
    char manifestUrl[192];
    snprintf(manifestUrl, sizeof(manifestUrl), "%s/ota/%s/ota.json", OTA_BASE_URL,
             identity.codename);
    DebugLog::log("OTA: fetching manifest url=%s", manifestUrl);
    esp_http_client_config_t config{};
    config.url = manifestUrl;
    config.timeout_ms = 15000;
    config.buffer_size = 512;
    // Use IDF's flash-resident certificate bundle. Parsing a 4 KB RSA root
    // certificate into heap here can fail while BLE and Wi-Fi coexist.
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = collectManifest;
    config.user_data = &body;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;
    const uint32_t started = millis();
    const esp_err_t result = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    DebugLog::log("OTA: manifest response status=%d result=0x%x bytes=%u elapsed_ms=%lu",
                  status, unsigned(result), unsigned(body.length),
                  static_cast<unsigned long>(millis() - started));
    if (result != ESP_OK || status != 200 || body.overflow) {
        DebugLog::log("OTA: manifest request failed status=%d result=0x%x", status, unsigned(result));
        return false;
    }
    if (!strstr(body.data, "\"schema\":1") ||
        !jsonString(body.data, "device_name", manifest.deviceName, sizeof(manifest.deviceName)) ||
        !jsonString(body.data, "codename", manifest.codename, sizeof(manifest.codename)) ||
        !jsonString(body.data, "manufacturer", manifest.manufacturer, sizeof(manifest.manufacturer)) ||
        !jsonString(body.data, "tag", manifest.tag, sizeof(manifest.tag)) ||
        !jsonString(body.data, "version", manifest.version, sizeof(manifest.version)) ||
        !jsonString(body.data, "firmware_url", manifest.firmwareUrl, sizeof(manifest.firmwareUrl)) ||
        !jsonString(body.data, "sha256", manifest.sha256, sizeof(manifest.sha256)) ||
        strlen(manifest.sha256) != 64) return false;

    for (const char* p = manifest.sha256; *p; ++p)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) return false;
    char expectedUrl[192];
    snprintf(expectedUrl, sizeof(expectedUrl), "%s/firmware/%s/%s.bin", OTA_BASE_URL,
             identity.codename, manifest.tag);
    if (strcmp(manifest.deviceName, identity.name) != 0 ||
        strcmp(manifest.codename, identity.codename) != 0 ||
        strcmp(manifest.manufacturer, identity.manufacturer) != 0 ||
        strncmp(manifest.tag, "ewp-", 4) != 0 || strcmp(expectedUrl, manifest.firmwareUrl) != 0 ||
        strcmp(manifest.tag, manifest.version) != 0) return false;

    const char* sizeField = strstr(body.data, "\"size\":");
    if (!sizeField) return false;
    char* end = nullptr;
    const unsigned long parsedSize = strtoul(sizeField + 7, &end, 10);
    if (end == sizeField + 7 || parsedSize == 0 || parsedSize > 2 * 1024 * 1024) return false;
    manifest.size = static_cast<size_t>(parsedSize);
    return true;
}

bool partitionMatchesManifest(const esp_partition_t* partition, size_t imageSize,
                              const char* expectedSha256) {
    if (!partition || imageSize > partition->size || strlen(expectedSha256) != 64) return false;
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    if (mbedtls_sha256_starts_ret(&sha, 0) != 0) {
        mbedtls_sha256_free(&sha);
        return false;
    }
    uint8_t bytes[512];
    size_t offset = 0;
    bool ok = true;
    while (offset < imageSize) {
        const size_t length = imageSize - offset < sizeof(bytes) ? imageSize - offset : sizeof(bytes);
        if (esp_partition_read(partition, offset, bytes, length) != ESP_OK ||
            mbedtls_sha256_update_ret(&sha, bytes, length) != 0) {
            ok = false;
            break;
        }
        offset += length;
        vTaskDelay(1);
    }
    uint8_t digest[32];
    if (ok) ok = mbedtls_sha256_finish_ret(&sha, digest) == 0;
    mbedtls_sha256_free(&sha);
    if (!ok) return false;
    static constexpr char HEX_DIGITS[] = "0123456789abcdef";
    char actual[65];
    for (size_t i = 0; i < sizeof(digest); ++i) {
        actual[i * 2] = HEX_DIGITS[digest[i] >> 4];
        actual[i * 2 + 1] = HEX_DIGITS[digest[i] & 0x0f];
    }
    actual[64] = '\0';
    return strcmp(actual, expectedSha256) == 0;
}

bool downloadFirmware(esp_http_client_handle_t client, esp_ota_handle_t otaHandle,
                      const OtaManifest& manifest, size_t& written) {
    RangeResponse response;
    uint8_t buffer[2048];
    constexpr size_t APP_DESC_OFFSET = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
    constexpr size_t APP_DESC_END = APP_DESC_OFFSET + sizeof(esp_app_desc_t);
    uint8_t metadata[APP_DESC_END]{};
    bool metadataChecked = false;
    unsigned retriesWithoutProgress = 0;
    const uint32_t started = millis();
    uint32_t lastProgressAt = started;
    size_t lastProgress = 0;

    while (written < manifest.size) {
        if (uint32_t(millis() - started) >= OTA_TRANSFER_TIMEOUT_MS) {
            DebugLog::log("OTA: transfer deadline exceeded at byte=%u", unsigned(written));
            return false;
        }
        const size_t requestStart = written;
        const size_t requestEnd = requestStart + OTA_RANGE_SIZE < manifest.size
                                      ? requestStart + OTA_RANGE_SIZE - 1
                                      : manifest.size - 1;
        char range[56];
        snprintf(range, sizeof(range), "bytes=%u-%u", unsigned(requestStart), unsigned(requestEnd));
        response = {};
        esp_err_t requestResult = esp_http_client_set_header(client, "Range", range);
        DebugLog::log("OTA: requesting bytes=%u-%u attempt=%u", unsigned(requestStart),
                      unsigned(requestEnd), retriesWithoutProgress + 1);

        if (requestResult == ESP_OK) requestResult = esp_http_client_open(client, 0);
        int contentLength = -1;
        if (requestResult == ESP_OK) {
            contentLength = esp_http_client_fetch_headers(client);
            const int status = esp_http_client_get_status_code(client);
            const size_t expectedLength = requestEnd - requestStart + 1;
            if (status == 0 || contentLength < 0) {
                requestResult = ESP_FAIL;
            } else if (status != 206 || !response.contentRangeSeen || !response.contentRangeValid ||
                       response.start != requestStart || response.end != requestEnd ||
                       response.total != manifest.size || contentLength != static_cast<int>(expectedLength)) {
                DebugLog::log("OTA: invalid range response status=%d content_length=%d range=%u-%u/%u expected=%u-%u/%u",
                              status, contentLength, unsigned(response.start), unsigned(response.end),
                              unsigned(response.total), unsigned(requestStart), unsigned(requestEnd),
                              unsigned(manifest.size));
                esp_http_client_close(client);
                return false;
            } else requestResult = ESP_OK;

            while (requestResult == ESP_OK && written <= requestEnd) {
                const int amount = esp_http_client_read(client, reinterpret_cast<char*>(buffer), sizeof(buffer));
                if (amount <= 0) {
                    requestResult = amount == 0 ? ESP_ERR_HTTP_EAGAIN : ESP_FAIL;
                    break;
                }
                const size_t chunkSize = static_cast<size_t>(amount);
                if (written + chunkSize > requestEnd + 1) {
                    DebugLog::log("OTA: server exceeded requested range at byte=%u", unsigned(written));
                    esp_http_client_close(client);
                    return false;
                }
                if (!metadataChecked && written < APP_DESC_END) {
                    const size_t copyStart = written;
                    const size_t copyEnd = copyStart + chunkSize < APP_DESC_END
                                               ? copyStart + chunkSize : APP_DESC_END;
                    memcpy(metadata + copyStart, buffer, copyEnd - copyStart);
                    if (copyEnd == APP_DESC_END) {
                        esp_app_desc_t remote{};
                        memcpy(&remote, metadata + APP_DESC_OFFSET, sizeof(remote));
                        metadataChecked = true;
                        if (strcmp(remote.version, manifest.version) != 0) {
                            DebugLog::log("OTA: image version mismatch manifest=%s image=%s",
                                          manifest.version, remote.version);
                            esp_http_client_close(client);
                            return false;
                        }
                        DebugLog::log("OTA: image metadata version=%s", remote.version);
                    }
                }
                const esp_err_t writeResult = esp_ota_write(otaHandle, buffer, chunkSize);
                if (writeResult != ESP_OK) {
                    DebugLog::log("OTA: flash write failed byte=%u status=0x%x",
                                  unsigned(written), unsigned(writeResult));
                    esp_http_client_close(client);
                    return false;
                }
                written += chunkSize;
                if (written - lastProgress >= OTA_RANGE_SIZE ||
                    uint32_t(millis() - lastProgressAt) >= 10000) {
                    DebugLog::log("OTA: transfer progress bytes=%u/%u elapsed_ms=%lu heap=%lu largest=%lu",
                                  unsigned(written), unsigned(manifest.size),
                                  static_cast<unsigned long>(millis() - started),
                                  static_cast<unsigned long>(ESP.getFreeHeap()),
                                  static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
                    lastProgress = written;
                    lastProgressAt = millis();
                }
            }
        }
        const int requestErrno = esp_http_client_get_errno(client);
        esp_http_client_close(client);
        if (written == requestEnd + 1) {
            retriesWithoutProgress = 0;
            continue;
        }

        if (written > requestStart) retriesWithoutProgress = 0;
        else ++retriesWithoutProgress;
        DebugLog::log("OTA: range interrupted byte=%u result=0x%x errno=%d retry=%u/%u",
                      unsigned(written), unsigned(requestResult), requestErrno,
                      retriesWithoutProgress, OTA_RANGE_RETRIES);
        if (retriesWithoutProgress >= OTA_RANGE_RETRIES) {
            DebugLog::log("OTA: range retry limit reached at byte=%u", unsigned(written));
            return false;
        }
        const unsigned backoffStep = retriesWithoutProgress ? retriesWithoutProgress - 1 : 0;
        const uint32_t backoffMs = 500U << (backoffStep > 2 ? 2 : backoffStep);
        vTaskDelay(pdMS_TO_TICKS(backoffMs));
    }
    return metadataChecked && written == manifest.size;
}

bool parseVersion(const char* value, uint32_t& major, uint32_t& minor, uint32_t& patch) {
    if (!value) return false;
    if (strncmp(value, "ewp-", 4) == 0) value += 4;
    char* end = nullptr;
    major = strtoul(value, &end, 10);
    if (end == value || *end++ != '.') return false;
    value = end;
    minor = strtoul(value, &end, 10);
    if (end == value || *end++ != '.') return false;
    value = end;
    patch = strtoul(value, &end, 10);
    return end != value;
}

bool isRemoteNewer(const char* remote, const char* current) {
    uint32_t rMajor, rMinor, rPatch, cMajor, cMinor, cPatch;
    if (!parseVersion(remote, rMajor, rMinor, rPatch) ||
        !parseVersion(current, cMajor, cMinor, cPatch)) return false;
    if (rMajor != cMajor) return rMajor > cMajor;
    if (rMinor != cMinor) return rMinor > cMinor;
    return rPatch > cPatch;
}

const char* imageStateName(esp_ota_img_states_t state) {
    switch (state) {
        case ESP_OTA_IMG_NEW: return "new";
        case ESP_OTA_IMG_PENDING_VERIFY: return "pending verify";
        case ESP_OTA_IMG_VALID: return "valid";
        case ESP_OTA_IMG_INVALID: return "invalid";
        case ESP_OTA_IMG_ABORTED: return "aborted";
        default: return "undefined";
    }
}

const esp_partition_t* findOtherOtaPartition() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (!running) return nullptr;
    const esp_partition_subtype_t subtype = running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0
        ? ESP_PARTITION_SUBTYPE_APP_OTA_1 : ESP_PARTITION_SUBTYPE_APP_OTA_0;
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, subtype, nullptr);
}

bool isBootable(const esp_partition_t* partition) {
    if (!partition) return false;
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(partition, &state) == ESP_OK &&
        (state == ESP_OTA_IMG_INVALID || state == ESP_OTA_IMG_ABORTED)) return false;
    // Validate segment extents before the IDF verifier: a failed prior OTA can
    // leave an E9 header with erased 0xFFFFFFFF segment lengths, which makes
    // esp_image_verify() log an alarming parser error on every UI redraw.
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

bool connectWifi() {
    const auto& config = WatchConfig::get();
    if (!config.wifiSsid[0]) return false;
    DebugLog::log("OTA: starting Wi-Fi association timeout_ms=%lu", static_cast<unsigned long>(WIFI_CONNECT_TIMEOUT_MS));
    auto& wifi = board::Board::current().getWifi();
    wifi.connectStation(config.wifiSsid, config.wifiPass, true);
    const uint32_t started = millis();
    uint32_t lastReport = started;
    while (wifi.state() != hal::WifiState::Connected && millis() - started < WIFI_CONNECT_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(200));
        if (uint32_t(millis() - lastReport) >= 4000) {
            DebugLog::log("OTA: waiting for Wi-Fi status=%d elapsed_ms=%lu",
                          int(wifi.state()), static_cast<unsigned long>(millis() - started));
            lastReport = millis();
        }
    }
    DebugLog::log("OTA: Wi-Fi status=%d heap=%lu largest=%lu", int(wifi.state()),
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    return wifi.state() == hal::WifiState::Connected;
}

void disconnectWifi() {
    board::Board::current().getWifi().disconnect(true);
}
} // namespace

OtaService& OtaService::instance() {
    static OtaService service;
    return service;
}

void OtaService::begin() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    DebugLog::log("OTA: booted slot=%s image_state=%s", runningSlot(), runningImageState());
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        pendingConfirmation_ = true;
        confirmationStartedMs_ = millis();
        DebugLog::log("OTA: candidate image running; confirmation window=%lu ms",
                      static_cast<unsigned long>(BOOT_CONFIRM_DELAY_MS));
    }
}

void OtaService::tick() {
    if (!pendingConfirmation_ ||
        static_cast<uint32_t>(millis() - confirmationStartedMs_) < BOOT_CONFIRM_DELAY_MS) {
        return;
    }

    const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
    if (result == ESP_OK) {
        pendingConfirmation_ = false;
        DebugLog::log("OTA: candidate image confirmed after stable runtime");
    } else {
        DebugLog::log("OTA: candidate confirmation failed status=0x%x", unsigned(result));
    }
}

bool OtaService::checkForUpdate() {
    UpdateState expected = updateState_.load();
    if (expected == UpdateState::Checking || expected == UpdateState::Installing) return false;
    updateState_.store(UpdateState::Checking);
    if (xTaskCreate(updateTask, "ota-check", 8192, this, 3, nullptr) != pdPASS) {
        updateState_.store(UpdateState::Failed);
        return false;
    }
    return true;
}

bool OtaService::installUpdate() {
    UpdateState expected = UpdateState::Available;
    if (!updateState_.compare_exchange_strong(expected, UpdateState::Installing)) return false;
    if (xTaskCreate(updateTask, "ota-install", 8192, this, 3, nullptr) != pdPASS) {
        updateState_.store(UpdateState::Available);
        return false;
    }
    return true;
}

void OtaService::resumeAfterCheck() {
    const UpdateState state = updateState_.load();
    if (updateTaskActive_.load() || state == UpdateState::Checking || state == UpdateState::Installing) {
        resumeComponentsRequested_.store(true);
        return;
    }
    auto& board = board::Board::current();
    board.getCompanionSource().resumeFromMaintenance();
    board.getBluetooth().resumeAfterMaintenance();
}

void OtaService::updateTask(void* context) {
    auto* self = static_cast<OtaService*>(context);
    const bool install = self->updateState_.load() == UpdateState::Installing;
    self->updateTaskActive_.store(true);
    self->runUpdate(install);
    self->updateTaskActive_.store(false);
    if (self->resumeComponentsRequested_.exchange(false)) {
        auto& board = board::Board::current();
        board.getCompanionSource().resumeFromMaintenance();
        board.getBluetooth().resumeAfterMaintenance();
    }
    vTaskDelete(nullptr);
}

void OtaService::runUpdate(bool install) {
    auto& bluetooth = board::Board::current().getBluetooth();
    if (!bluetooth.suspendForMaintenance()) {
        updateState_.store(UpdateState::Failed);
        DebugLog::log("OTA: cannot suspend BLE for maintenance");
        return;
    }
    auto& source = board::Board::current().getCompanionSource();
    if (!source.pauseForMaintenance()) {
        source.resumeFromMaintenance();
        bluetooth.resumeAfterMaintenance();
        updateState_.store(UpdateState::Failed);
        DebugLog::log("OTA: companion source could not pause for maintenance");
        return;
    }
    bool keepCommunicationPaused = false;
    struct ResumeComponents {
        hal::IBluetooth& bluetooth;
        hal::ICompanionSource& source;
        bool& keepPaused;
        std::atomic<bool>& resumeRequested;
        ~ResumeComponents() {
            if (!keepPaused || resumeRequested.exchange(false)) {
                source.resumeFromMaintenance();
                bluetooth.resumeAfterMaintenance();
            }
        }
    } resumeComponents{bluetooth, source, keepCommunicationPaused, resumeComponentsRequested_};

    DebugLog::log("OTA: BLE suspended; heap=%u largest=%u",
                  unsigned(ESP.getFreeHeap()),
                  unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    // With the flash-resident certificate bundle, OTA can proceed with a
    // smaller contiguous block than the old PEM-based TLS path required.
    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 12 * 1024 ||
        ESP.getFreeHeap() < 45000) {
        updateState_.store(UpdateState::Failed);
        DebugLog::log("OTA: update stopped; insufficient free heap");
        return;
    }
    if (!connectWifi()) {
        updateState_.store(UpdateState::Failed);
        DebugLog::log("OTA: update check failed; Wi-Fi unavailable");
        disconnectWifi();
        return;
    }

    DebugLog::log("OTA: Wi-Fi ready; starting manifest request heap=%lu largest=%lu",
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    OtaManifest manifest;
    if (!fetchManifest(manifest)) {
        updateState_.store(UpdateState::Failed);
        DebugLog::log("OTA: manifest rejected or unreachable (epoch=%ld)", static_cast<long>(time(nullptr)));
        disconnectWifi();
        return;
    }
    strlcpy(updateVersion_, manifest.version, sizeof(updateVersion_));
    const esp_app_desc_t* current = esp_ota_get_app_description();
    const bool remoteNewer = current && isRemoteNewer(manifest.version, current->version);
    if (!install) {
        DebugLog::log("OTA: manifest current=%s available=%s size=%lu sha256=%s", current ? current->version : "unknown",
                      manifest.version, static_cast<unsigned long>(manifest.size), manifest.sha256);
        disconnectWifi();
        // Keep the radio off between "Update available" and Install. Restarting
        // BLE here fragments the heap again before the next button action.
        keepCommunicationPaused = remoteNewer;
        updateState_.store(remoteNewer ? UpdateState::Available : UpdateState::UpToDate);
        return;
    }
    if (!remoteNewer) {
        disconnectWifi();
        updateState_.store(UpdateState::UpToDate);
        return;
    }

    esp_http_client_config_t httpConfig{};
    httpConfig.url = manifest.firmwareUrl;
    httpConfig.timeout_ms = 30000;
    httpConfig.buffer_size = 1024;
    httpConfig.buffer_size_tx = 512;
    httpConfig.crt_bundle_attach = esp_crt_bundle_attach;
    httpConfig.event_handler = rangeHttpEvent;
    RangeResponse rangeResponse;
    httpConfig.user_data = &rangeResponse;
    DebugLog::log("OTA: preparing resumable ranged transfer range=%d timeout_ms=%d heap=%lu largest=%lu",
                  OTA_RANGE_SIZE, httpConfig.timeout_ms,
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    esp_http_client_handle_t client = esp_http_client_init(&httpConfig);
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    esp_ota_handle_t otaHandle = 0;
    esp_err_t result = client && target && manifest.size <= target->size
                           ? esp_ota_begin(target, manifest.size, &otaHandle)
                           : ESP_ERR_INVALID_SIZE;
    if (result != ESP_OK) {
        DebugLog::log("OTA: could not begin update target=%s size=%u status=0x%x heap=%lu largest=%lu",
                      target ? target->label : "unavailable", unsigned(manifest.size), unsigned(result),
                      static_cast<unsigned long>(ESP.getFreeHeap()),
                      static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    }
    size_t imageLength = 0;
    if (result == ESP_OK) {
        DebugLog::log("OTA: target slot=%s erase/write started size=%u",
                      target->label, unsigned(manifest.size));
        if (!downloadFirmware(client, otaHandle, manifest, imageLength)) {
            esp_ota_abort(otaHandle);
            result = ESP_FAIL;
        } else {
            DebugLog::log("OTA: transfer complete bytes=%u; validating image", unsigned(imageLength));
            result = esp_ota_end(otaHandle);
            DebugLog::log("OTA: image validation status=0x%x", unsigned(result));
            if (result == ESP_OK && !partitionMatchesManifest(target, manifest.size, manifest.sha256)) {
                DebugLog::log("OTA: image SHA-256 does not match manifest");
                result = ESP_ERR_OTA_VALIDATE_FAILED;
            }
            if (result == ESP_OK) {
                result = esp_ota_set_boot_partition(target);
                DebugLog::log("OTA: next-boot slot selection status=0x%x", unsigned(result));
            }
        }
    }
    if (client) esp_http_client_cleanup(client);
    if (result == ESP_OK) {
        DebugLog::log("OTA: installed version=%s; rebooting", updateVersion_);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
    updateState_.store(UpdateState::Failed);
    DebugLog::log("OTA: install failed status=0x%x", unsigned(result));
    disconnectWifi();
}

const char* OtaService::updateMessage() const {
    switch (updateState_.load()) {
        case UpdateState::Checking: return "Checking releases";
        case UpdateState::UpToDate: return "Already up to date";
        case UpdateState::Available: return "Update available";
        case UpdateState::Installing: return "Installing update";
        case UpdateState::Failed: return "Update failed; retry";
        default: return "Ready to check";
    }
}

const char* OtaService::runningSlot() const {
    const esp_partition_t* partition = esp_ota_get_running_partition();
    if (!partition) return "unknown";
    if (partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) return "ota_0";
    if (partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) return "ota_1";
    return "other";
}

const char* OtaService::runningImageState() const {
    const esp_partition_t* partition = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    return partition && esp_ota_get_state_partition(partition, &state) == ESP_OK
               ? imageStateName(state) : "unavailable";
}

const char* OtaService::runningVersion() const {
    const esp_app_desc_t* descriptor = esp_ota_get_app_description();
    return descriptor ? descriptor->version : "unknown";
}

const char* OtaService::otherSlot() const {
    const esp_partition_t* partition = findOtherOtaPartition();
    if (!partition) return "none";
    return partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ? "ota_0" : "ota_1";
}

const char* OtaService::otherImageState() const {
    const esp_partition_t* partition = findOtherOtaPartition();
    esp_ota_img_states_t state;
    if (!partition) return "missing";
    if (esp_ota_get_state_partition(partition, &state) != ESP_OK) return "undefined";
    return imageStateName(state);
}

bool OtaService::otherSlotBootable() const {
    return isBootable(findOtherOtaPartition());
}

bool OtaService::selectOtherSlot() {
    const esp_partition_t* partition = findOtherOtaPartition();
    if (!isBootable(partition)) return false;
    const esp_err_t result = esp_ota_set_boot_partition(partition);
    if (result == ESP_OK) {
        DebugLog::log("OTA: selected alternate slot=%s for next boot",
                      partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ? "ota_0" : "ota_1");
        return true;
    }
    DebugLog::log("OTA: alternate slot selection failed status=0x%x", unsigned(result));
    return false;
}

} // namespace services
} // namespace ersa
