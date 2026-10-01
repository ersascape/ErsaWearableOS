#include "hal/esp32/esp32_bluetooth.h"
#include <string.h>

#if defined(ARDUINO) && defined(CONFIG_IDF_TARGET_ESP32C3)
#include <Arduino.h>
#include <atomic>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "hal/esp32/esp32_apple_ble.h"
#include "ersa/board/board.h"
#include "core/debug_log.h"

#define SERVICE_UUID        "0000FFE0-0000-1000-8000-00805F9B34FB"
#define CHAR_CALL_UUID      "0000FFE1-0000-1000-8000-00805F9B34FB"
#define CHAR_MEDIA_UUID     "0000FFE2-0000-1000-8000-00805F9B34FB"
#define CHAR_RECENTS_UUID   "0000FFE3-0000-1000-8000-00805F9B34FB"

namespace {
constexpr uint16_t FAST_ADV_INTERVAL = 32; // 20 ms, BLE units of 0.625 ms
constexpr uint16_t SLOW_ADV_INTERVAL = 874; // 546.25 ms, Apple QA1931 value
constexpr uint32_t FAST_ADV_DURATION_MS = 30000;
}

namespace ersa {
namespace hal {

class BleSecCallbacks : public BLESecurityCallbacks {
public:
    using AuthCallback = void (*)(const esp_ble_auth_cmpl_t&, void*);
    BleSecCallbacks(AuthCallback callback, void* user) : callback_(callback), user_(user) {}
    AuthCallback callback_;
    void* user_;
    uint32_t onPassKeyRequest() override { return 123456; }
    void onPassKeyNotify(uint32_t pass_key) override { (void)pass_key; }
    bool onConfirmPIN(uint32_t pass_key) override { (void)pass_key; return true; }
    bool onSecurityRequest() override { return true; }
    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override {
        DebugLog::log("BLE: Auth complete success=%d fail_reason=0x%x", cmpl.success, cmpl.fail_reason);
        if (callback_) callback_(cmpl, user_);
    }
};

class Esp32Bluetooth::Impl : public BLEServerCallbacks, public BLECharacteristicCallbacks {
public:
    Esp32Bluetooth* parent_{nullptr};
    BLEServer* pServer_{nullptr};
    BLEService* pService_{nullptr};
    BLECharacteristic* pCallChar_{nullptr};
    BLECharacteristic* pMediaChar_{nullptr};
    BLECharacteristic* pRecentsChar_{nullptr};
    Esp32AppleClient appleClient_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> advertising_{false}, advertisingPending_{false};
    std::atomic<bool> slowAdvertising_{false}, slowRestartPending_{false};
    std::atomic<bool> companionCalls_{false}, companionMedia_{false};
    uint16_t connectionId_{0};
    bool initialized_{false};
    std::atomic<uint32_t> advertiseAfterMs_{0};
    std::atomic<uint32_t> advertisingStartedAtMs_{0};
    static Impl*& current() { static Impl* instance = nullptr; return instance; }
    static void gapEvent(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
        auto* self = current();
        if (!self || !param) return;
        if (event == ESP_GAP_BLE_ADV_START_COMPLETE_EVT) {
            const bool success = param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS;
            self->advertising_ = success;
            self->advertisingPending_ = !success && !self->connected_;
            if (success) self->advertisingStartedAtMs_ = millis();
            DebugLog::log("BLE: advertising start %s status=0x%x", success ? "ok" : "failed",
                          unsigned(param->adv_start_cmpl.status));
        } else if (event == ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT) {
            self->advertising_ = false;
            if (self->slowRestartPending_.exchange(false)) {
                self->advertisingPending_ = true;
                self->advertiseAfterMs_ = millis() + 250;
            } else if (!self->connected_ &&
                       !(self->parent_ && self->parent_->maintenanceSuspended_)) {
                // Advertising can stop outside the fast-to-slow transition
                // (for example after a controller-side radio event). Treat
                // every unexpected stop as recoverable.
                self->advertisingPending_ = true;
                self->advertiseAfterMs_ = millis() + 750;
                DebugLog::log("BLE: advertising stopped while disconnected; scheduling retry");
            }
        }
    }
    esp_bd_addr_t peer_{};
    esp_ble_addr_type_t peerType_{BLE_ADDR_TYPE_RANDOM};
    portMUX_TYPE authMux_ = portMUX_INITIALIZER_UNLOCKED;
    bool discoveryReadyForAuth_{false};
    bool pendingAuth_{false};
    bool pendingAuthSuccess_{false};
    static void authenticated(const esp_ble_auth_cmpl_t& auth, void* user) {
        auto* self = static_cast<Impl*>(user);
        bool releaseWorker = false;
        portENTER_CRITICAL(&self->authMux_);
        if (self->connected_ && self->discoveryReadyForAuth_) {
            releaseWorker = true;
        } else {
            self->pendingAuth_ = true;
            self->pendingAuthSuccess_ = auth.success;
        }
        portEXIT_CRITICAL(&self->authMux_);
        if (releaseWorker) {
            self->appleClient_.authenticationComplete(auth.success);
            if (auth.success) DebugLog::log("BLE: encrypted link ready; Apple worker released");
            else {
                DebugLog::log("BLE: authentication failed; disconnecting so advertising can recover");
                esp_ble_gap_disconnect(self->peer_);
            }
        } else {
            DebugLog::log("BLE: authentication result latched until Apple discovery is ready");
        }
        // Do not compare an identity address with a potentially private connection
        // address. The current peripheral connection owns this auth completion.
    }

    void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) override {
        (void)pServer;
        connected_ = true;
        portENTER_CRITICAL(&authMux_);
        discoveryReadyForAuth_ = false;
        portEXIT_CRITICAL(&authMux_);
        if (param) connectionId_ = param->connect.conn_id;
        advertising_ = false;
        advertisingPending_ = false;
        slowRestartPending_ = false;
        DebugLog::log("BLE: Central connected");
        if (param) {
            memcpy(peer_, param->connect.remote_bda, sizeof(peer_));
            peerType_ = param->connect.ble_addr_type;
            // BLEDevice already requests encryption before this callback.
            // A second request here produced "earlier enc was not done".
            DebugLog::log("BLE: Apple discovery scheduled (addrType=%u)", unsigned(peerType_));
            appleClient_.startDiscovery(peer_, peerType_);
        }
        bool releaseWorker = false;
        bool authSuccess = false;
        portENTER_CRITICAL(&authMux_);
        discoveryReadyForAuth_ = true;
        if (pendingAuth_) {
            releaseWorker = true;
            authSuccess = pendingAuthSuccess_;
            pendingAuth_ = false;
        }
        portEXIT_CRITICAL(&authMux_);
        if (releaseWorker) {
            appleClient_.authenticationComplete(authSuccess);
            DebugLog::log("BLE: latched authentication result delivered to Apple worker success=%d", authSuccess);
            if (!authSuccess) {
                DebugLog::log("BLE: authentication failed; disconnecting so advertising can recover");
                esp_ble_gap_disconnect(peer_);
            }
        }
        if (parent_ && parent_->connCb_) {
            parent_->connCb_(true, parent_->connUserData_);
        }
    }

    void onDisconnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) override {
        (void)pServer;
        connected_ = false;
        portENTER_CRITICAL(&authMux_);
        discoveryReadyForAuth_ = false;
        pendingAuth_ = false;
        portEXIT_CRITICAL(&authMux_);
        advertising_ = false;
        advertisingPending_ = !(parent_ && parent_->maintenanceSuspended_);
        slowAdvertising_ = false;
        slowRestartPending_ = false;
        advertiseAfterMs_ = millis() + 750;
        companionCalls_ = false; companionMedia_ = false;
        appleClient_.stop();
        DebugLog::log("BLE: Central disconnected reason=0x%02x%s", param ? unsigned(param->disconnect.reason) : 0,
                      (parent_ && parent_->maintenanceSuspended_) ? "; maintenance suspend" : "; restarting advertising");
        if (parent_ && parent_->connCb_) {
            parent_->connCb_(false, parent_->connUserData_);
        }
        // BLEServer removes this connection only after callbacks return.
        // Advertising from here can be rejected while the link still exists.
    }

    void onWrite(BLECharacteristic* pCharacteristic) override {
        std::string rxVal = pCharacteristic->getValue();
        if (rxVal.empty()) return;

        if (pCharacteristic == pCallChar_) {
            companionCalls_ = true;
            // First byte = action: 0=Incoming, 1=Answered, 2=Rejected, 3=Ended
            uint8_t actionByte = static_cast<uint8_t>(rxVal[0]);
            CompanionCallAction action = CompanionCallAction::Incoming;
            if (actionByte == 1) action = CompanionCallAction::Answered;
            else if (actionByte == 2) action = CompanionCallAction::Rejected;
            else if (actionByte == 3) action = CompanionCallAction::Ended;

            char caller[32] = "";
            char number[20] = "";

            if (rxVal.length() > 1) {
                const char* payload = rxVal.c_str() + 1;
                const char* sep = strchr(payload, '\t');
                if (!sep) sep = strchr(payload, ',');

                if (sep) {
                    size_t cLen = sep - payload;
                    if (cLen >= sizeof(caller)) cLen = sizeof(caller) - 1;
                    strncpy(caller, payload, cLen);
                    caller[cLen] = '\0';
                    strncpy(number, sep + 1, sizeof(number) - 1);
                    number[sizeof(number) - 1] = '\0';
                } else {
                    strncpy(caller, payload, sizeof(caller) - 1);
                    caller[sizeof(caller) - 1] = '\0';
                }
            }

            DebugLog::log("BLE: Call event action=%d caller='%s' number='%s'",
                          int(action), caller, number);

            if (parent_ && parent_->callCb_) {
                parent_->callCb_(action, caller, number, parent_->callUserData_);
            }
        } else if (pCharacteristic == pMediaChar_) {
            companionMedia_ = true;
            // First byte: 1=Playing, 0=Paused
            bool playing = (static_cast<uint8_t>(rxVal[0]) == 1);
            char title[32] = "";
            char artist[32] = "";

            if (rxVal.length() > 1) {
                const char* payload = rxVal.c_str() + 1;
                const char* sep = strchr(payload, '\t');
                if (sep) {
                    size_t tLen = sep - payload;
                    if (tLen >= sizeof(title)) tLen = sizeof(title) - 1;
                    strncpy(title, payload, tLen);
                    title[tLen] = '\0';
                    strncpy(artist, sep + 1, sizeof(artist) - 1);
                    artist[sizeof(artist) - 1] = '\0';
                } else {
                    strncpy(title, payload, sizeof(title) - 1);
                    title[sizeof(title) - 1] = '\0';
                }
            }

            DebugLog::log("BLE: Media track: playing=%d title='%s' artist='%s'",
                          int(playing), title, artist);

            if (parent_ && parent_->mediaCb_) {
                parent_->mediaCb_(playing, title, artist, parent_->mediaUserData_);
            }
        }
    }
};

Esp32Bluetooth::Esp32Bluetooth() : pImpl_(new Impl()) {
    pImpl_->parent_ = this;
}

Esp32Bluetooth::~Esp32Bluetooth() {
    if (Impl::current() == pImpl_) Impl::current() = nullptr;
    delete pImpl_;
}

Result<void> Esp32Bluetooth::init() {
    if (!pImpl_) {
        pImpl_ = new (std::nothrow) Impl();
        if (!pImpl_) return Result<void>(ErrorCode::OutOfMemory, "BLE driver state");
        pImpl_->parent_ = this;
    }
    if (pImpl_->initialized_) {
        return Result<void>();
    }
    const auto* selectedBoard = ersa::board::Board::currentOrNull();
    if (!selectedBoard) return Result<void>(ErrorCode::NotFound, "selected board identity is unavailable");
    const auto& identity = selectedBoard->getDeviceInfo();
    DebugLog::log("BLE: initializing '%s' BLE peripheral", identity.name);
    BLEDevice::init(identity.name);
    Impl::current() = pImpl_;
    BLEDevice::setCustomGapHandler(&Impl::gapEvent);

    pImpl_->pServer_ = BLEDevice::createServer();
    pImpl_->pServer_->setCallbacks(pImpl_);
    pImpl_->appleClient_.setCallCallback(callCb_, callUserData_);
    pImpl_->appleClient_.setMediaCallback(mediaCb_, mediaUserData_);
    pImpl_->appleClient_.setNotificationCallback(notifCb_, notifUserData_);
    pImpl_->appleClient_.setTimeCallback(timeCb_, timeUserData_);

    // Custom Ersa Service (0xFFE0) with Call, Media & Recents characteristics
    pImpl_->pService_ = pImpl_->pServer_->createService(SERVICE_UUID);

    // Call Characteristic
    pImpl_->pCallChar_ = pImpl_->pService_->createCharacteristic(
        CHAR_CALL_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pImpl_->pCallChar_->addDescriptor(new BLE2902());
    pImpl_->pCallChar_->setCallbacks(pImpl_);

    // Media Characteristic
    pImpl_->pMediaChar_ = pImpl_->pService_->createCharacteristic(
        CHAR_MEDIA_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pImpl_->pMediaChar_->addDescriptor(new BLE2902());
    pImpl_->pMediaChar_->setCallbacks(pImpl_);

    // Recents Characteristic
    pImpl_->pRecentsChar_ = pImpl_->pService_->createCharacteristic(
        CHAR_RECENTS_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pImpl_->pRecentsChar_->addDescriptor(new BLE2902());
    pImpl_->pRecentsChar_->setCallbacks(pImpl_);

    pImpl_->pService_->start();

    // Standard Device Information Service (0x180A)
    BLEService* pDisService = pImpl_->pServer_->createService(BLEUUID((uint16_t)0x180A));
    BLECharacteristic* pMfrChar = pDisService->createCharacteristic(
        BLEUUID((uint16_t)0x2A29), BLECharacteristic::PROPERTY_READ);
    pMfrChar->setValue(identity.manufacturer);
    BLECharacteristic* pModelChar = pDisService->createCharacteristic(
        BLEUUID((uint16_t)0x2A24), BLECharacteristic::PROPERTY_READ);
    pModelChar->setValue(identity.name);
    BLECharacteristic* pFwChar = pDisService->createCharacteristic(
        BLEUUID((uint16_t)0x2A26), BLECharacteristic::PROPERTY_READ);
    pFwChar->setValue("1.0.0");
    pDisService->start();

    // Standard Battery Service (0x180F)
    BLEService* pBatService = pImpl_->pServer_->createService(BLEUUID((uint16_t)0x180F));
    BLECharacteristic* pBatLevelChar = pBatService->createCharacteristic(
        BLEUUID((uint16_t)0x2A19),
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    pBatLevelChar->addDescriptor(new BLE2902());
    uint8_t battPct = 100;
    pBatLevelChar->setValue(&battPct, 1);
    pBatService->start();

    pImpl_->pCallChar_->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);
    pImpl_->pMediaChar_->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);

    // Configure BLE Security Bonding for native iOS Pairing & ANCS / AMS access
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
    BLEDevice::setSecurityCallbacks(new BleSecCallbacks(&Impl::authenticated, pImpl_));
    BLESecurity* pSecurity = new BLESecurity();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
    pSecurity->setCapability(ESP_IO_CAP_NONE);
    pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    pImpl_->initialized_ = true;
    return Result<void>();
}

void Esp32Bluetooth::startAdvertising() {
    if (!pImpl_ || maintenanceSuspended_) return;
    if (!pImpl_->initialized_ || pImpl_->connected_) return;
    pImpl_->advertising_ = false;
    pImpl_->slowAdvertising_ = false;
    pImpl_->slowRestartPending_ = false;
    pImpl_->advertisingPending_ = true;
    pImpl_->advertiseAfterMs_ = millis() + 250;
    DebugLog::log("BLE: advertising scheduled");
}

void Esp32Bluetooth::tick() {
    if (maintenanceSuspended_ || !pImpl_) return;
    tickUsers_.fetch_add(1, std::memory_order_acq_rel);
    if (maintenanceSuspended_ || !pImpl_) {
        tickUsers_.fetch_sub(1, std::memory_order_release);
        return;
    }
    struct TickExit { std::atomic<uint32_t>& users; ~TickExit() { users.fetch_sub(1, std::memory_order_release); } } tickExit{tickUsers_};
    if (!pImpl_->initialized_) return;
    const bool sourceAvailable = isAvailable();
    if (sourceAvailable != lastSourceAvailability_) {
        lastSourceAvailability_ = sourceAvailable;
        if (availabilityCb_) availabilityCb_(sourceAvailable, availabilityUserData_);
    }
    if (pImpl_->connected_) return;
    if (pImpl_->advertising_) {
        const uint32_t elapsed = uint32_t(millis() - pImpl_->advertisingStartedAtMs_.load());
        if (!pImpl_->slowAdvertising_ && !pImpl_->slowRestartPending_ &&
            elapsed >= FAST_ADV_DURATION_MS) {
            pImpl_->slowAdvertising_ = true;
            pImpl_->slowRestartPending_ = true;
            pImpl_->advertisingPending_ = false;
            DebugLog::log("BLE: switching to slow advertising after %lu ms",
                          static_cast<unsigned long>(elapsed));
            BLEDevice::stopAdvertising();
        }
        return;
    }
    if (!pImpl_->advertisingPending_ ||
        int32_t(millis() - pImpl_->advertiseAfterMs_.load()) < 0) return;
    pImpl_->advertiseAfterMs_ = millis() + 5000;
    beginAdvertising();
}

void Esp32Bluetooth::beginAdvertising() {
    if (!pImpl_ || maintenanceSuspended_ || pImpl_->connected_) return;
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();

    // Primary Advertisement Data: Flags + ANCS 128-bit Service Solicitation (21 bytes <= 31 max)
    BLEAdvertisementData advData;
    advData.setFlags(0x06); // General Discoverable + BR/EDR Not Supported

    // 128-bit ANCS Solicitation UUID: 7905f431-b5ce-4e99-a40f-4b1e122d00d0
    BLEUUID ancsUUID("7905f431-b5ce-4e99-a40f-4b1e122d00d0");
    char solData[2];
    solData[0] = 17;   // Length of AD element (1 byte type + 16 bytes UUID)
    solData[1] = 0x15; // AD Type: 128-bit Service Solicitation
    advData.addData(std::string(solData, 2) + std::string(reinterpret_cast<const char*>(ancsUUID.getNative()->uuid.uuid128), 16));
    pAdvertising->setAdvertisementData(advData);

    // Scan response advertises the board-provided device name and custom service.
    BLEAdvertisementData scanResponse;
    scanResponse.setName(getDeviceName());
    scanResponse.setCompleteServices(BLEUUID((uint16_t)0xFFE0));
    pAdvertising->setScanResponseData(scanResponse);

    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMaxPreferred(0x12);
    // Apple recommends 20 ms for the initial 30 seconds, followed by exactly
    // 546.25 ms. The state machine switches intervals after fast discovery.
    const uint16_t interval = pImpl_->slowAdvertising_ ? SLOW_ADV_INTERVAL : FAST_ADV_INTERVAL;
    pAdvertising->setMinInterval(interval);
    pAdvertising->setMaxInterval(interval);
    BLEDevice::startAdvertising();
    DebugLog::log("BLE: advertising requested with ANCS solicitation interval=%s addr=%s",
                  pImpl_->slowAdvertising_ ? "546.25ms" : "20ms", getDeviceAddress());
}

void Esp32Bluetooth::stopAdvertising() {
    if (!pImpl_ || maintenanceSuspended_) return;
    pImpl_->advertisingPending_ = false;
    pImpl_->advertising_ = false;
    pImpl_->slowRestartPending_ = false;
    BLEDevice::stopAdvertising();
}

bool Esp32Bluetooth::suspendForMaintenance() {
    if (maintenanceSuspended_) return true;
    maintenanceSuspended_ = true;
    if (!pImpl_) return true;
    pImpl_->advertisingPending_ = false;
    pImpl_->slowRestartPending_ = false;
    BLEDevice::stopAdvertising();
    const uint32_t waitStarted = millis();
    while (tickUsers_.load(std::memory_order_acquire) != 0 &&
           uint32_t(millis() - waitStarted) < 1000) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (tickUsers_.load(std::memory_order_acquire) != 0) {
        maintenanceSuspended_ = false;
        startAdvertising();
        DebugLog::log("BLE: maintenance suspend failed; driver tick still active");
        return false;
    }
    if (pImpl_->connected_ && pImpl_->pServer_) {
        // GAP disconnect explicitly terminates the radio link. The Arduino
        // wrapper's BLEServer::disconnect only calls gatts_close and hides
        // its return status; some central connections remain up after that.
        esp_err_t disconnectResult = esp_ble_gap_disconnect(pImpl_->peer_);
        DebugLog::log("BLE: OTA disconnect requested via GAP status=0x%x",
                      unsigned(disconnectResult));
        uint32_t disconnectStarted = millis();
        while (pImpl_->connected_ && uint32_t(millis() - disconnectStarted) < 3000) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (pImpl_->connected_) {
            pImpl_->pServer_->disconnect(pImpl_->connectionId_);
            DebugLog::log("BLE: OTA disconnect fallback via GATT close conn=%u",
                          unsigned(pImpl_->connectionId_));
            disconnectStarted = millis();
            while (pImpl_->connected_ && uint32_t(millis() - disconnectStarted) < 2000) {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
        if (pImpl_->connected_) {
            maintenanceSuspended_ = false;
            startAdvertising();
            DebugLog::log("BLE: maintenance suspend failed; central did not disconnect");
            return false;
        }
    }
    // Keep the NimBLE/Bludroid host and GATT server alive. Deinitializing and
    // rebuilding Arduino BLE invalidates library-owned client/service objects;
    // reconnecting during OTA check used to panic in descriptor cleanup.
    DebugLog::log("BLE: suspended for OTA maintenance; host retained");
    return true;
}

bool Esp32Bluetooth::pauseForMaintenance() {
    if (!pImpl_) return true;
    if (!pImpl_->appleClient_.suspendForMaintenance(5000)) {
        DebugLog::log("BLE source: maintenance pause failed; protocol worker did not stop");
        return false;
    }
    DebugLog::log("BLE source: protocol worker resources released for maintenance");
    return true;
}

void Esp32Bluetooth::resumeFromMaintenance() {
    // The next authenticated peer connection recreates source queues and its
    // worker. No protocol-specific discovery is restarted here.
}

void Esp32Bluetooth::resumeAfterMaintenance() {
    if (!maintenanceSuspended_) return;
    maintenanceSuspended_ = false;
    startAdvertising();
    DebugLog::log("BLE: advertising resumed after OTA maintenance");
}

bool Esp32Bluetooth::isAdvertising() const { return pImpl_ && pImpl_->advertising_; }

uint32_t Esp32Bluetooth::nextWakeDelayMs(uint32_t nowMs) const {
    if (!pImpl_ || maintenanceSuspended_ || !pImpl_->initialized_ || pImpl_->connected_) return UINT32_MAX;
    if (pImpl_->slowRestartPending_) return 250;
    if (pImpl_->advertising_) {
        if (pImpl_->slowAdvertising_) return UINT32_MAX;
        const uint32_t elapsed = uint32_t(nowMs - pImpl_->advertisingStartedAtMs_.load());
        return elapsed >= FAST_ADV_DURATION_MS ? 0 : FAST_ADV_DURATION_MS - elapsed;
    }
    if (!pImpl_->advertisingPending_) return UINT32_MAX;
    const int32_t remaining = int32_t(pImpl_->advertiseAfterMs_.load() - nowMs);
    return remaining > 0 ? uint32_t(remaining) : 0;
}

const char* Esp32Bluetooth::getDeviceName() const {
    const auto* board = ersa::board::Board::currentOrNull();
    return board ? board->getDeviceInfo().name : "Unknown Device";
}

const char* Esp32Bluetooth::getDeviceAddress() const {
    static char s_addrBuf[24] = "00:00:00:00:00:00";
    std::string s = BLEDevice::getAddress().toString();
    if (!s.empty()) {
        strncpy(s_addrBuf, s.c_str(), sizeof(s_addrBuf) - 1);
        s_addrBuf[sizeof(s_addrBuf) - 1] = '\0';
    }
    return s_addrBuf;
}

bool Esp32Bluetooth::isConnected() const {
    return pImpl_ && pImpl_->connected_;
}

const char* Esp32Bluetooth::sourceId() const { return "apple-ancs-ams-cts"; }
bool Esp32Bluetooth::isAvailable() const {
    if (!pImpl_ || !pImpl_->connected_) return false;
    const CompanionCapabilities caps = capabilities();
    return caps.notifications || caps.media || caps.calls || caps.timeSync;
}
CompanionCapabilities Esp32Bluetooth::capabilities() const {
    CompanionCapabilities caps{};
    if (!pImpl_ || !pImpl_->connected_) return caps;
    caps.notifications = pImpl_->appleClient_.isAncsActive();
    caps.media = pImpl_->appleClient_.isAmsActive() || pImpl_->companionMedia_;
    caps.calls = pImpl_->appleClient_.isAncsActive() || pImpl_->companionCalls_;
    caps.answerReject = caps.calls;
    caps.hangup = pImpl_->companionCalls_;
    caps.dial = pImpl_->companionCalls_;
    caps.remoteDismiss = pImpl_->appleClient_.isAncsActive();
    caps.timeSync = pImpl_->appleClient_.isCtsActive();
    return caps;
}

void Esp32Bluetooth::setCallCallback(CompanionCallCallback cb, void* userData) {
    callCb_ = cb;
    callUserData_ = userData;
    if (pImpl_) {
        pImpl_->appleClient_.setCallCallback(cb, userData);
    }
}

void Esp32Bluetooth::setMediaCallback(CompanionMediaCallback cb, void* userData) {
    mediaCb_ = cb;
    mediaUserData_ = userData;
    if (pImpl_) {
        pImpl_->appleClient_.setMediaCallback(cb, userData);
    }
}

void Esp32Bluetooth::setConnectionCallback(BleConnectionCallback cb, void* userData) {
    connCb_ = cb;
    connUserData_ = userData;
}

void Esp32Bluetooth::setNotificationCallback(CompanionNotificationCallback cb, void* userData) {
    notifCb_ = cb;
    notifUserData_ = userData;
    if (pImpl_) {
        pImpl_->appleClient_.setNotificationCallback(cb, userData);
    }
}

void Esp32Bluetooth::setTimeCallback(CompanionTimeCallback cb, void* userData) {
    timeCb_ = cb;
    timeUserData_ = userData;
    if (pImpl_) pImpl_->appleClient_.setTimeCallback(cb, userData);
}

void Esp32Bluetooth::setAvailabilityCallback(CompanionAvailabilityCallback cb, void* userData) {
    availabilityCb_ = cb;
    availabilityUserData_ = userData;
}

bool Esp32Bluetooth::acceptCall() {
    DebugLog::log("BLE: Command -> ACCEPT CALL");
    if (pImpl_ && capabilities().answerReject) {
        if (pImpl_->appleClient_.isAncsActive()) {
            pImpl_->appleClient_.acceptCall();
            return true;
        }
        if (pImpl_->pCallChar_ && pImpl_->connected_) {
            uint8_t val = 0x01; // Accept
            pImpl_->pCallChar_->setValue(&val, 1);
            pImpl_->pCallChar_->notify();
            return true;
        }
    }
    return false;
}

bool Esp32Bluetooth::rejectCall() {
    DebugLog::log("BLE: Command -> REJECT CALL");
    if (pImpl_ && capabilities().answerReject) {
        if (pImpl_->appleClient_.isAncsActive()) {
            pImpl_->appleClient_.rejectCall();
            return true;
        }
        if (pImpl_->pCallChar_ && pImpl_->connected_) {
            uint8_t val = 0x02; // Reject
            pImpl_->pCallChar_->setValue(&val, 1);
            pImpl_->pCallChar_->notify();
            return true;
        }
    }
    return false;
}

bool Esp32Bluetooth::hangupCall() {
    DebugLog::log("BLE: Command -> HANG UP CALL");
    if (pImpl_) {
        if (!capabilities().hangup) return false;
        if (pImpl_->pCallChar_ && pImpl_->connected_) {
            uint8_t val = 0x02; // Hangup
            pImpl_->pCallChar_->setValue(&val, 1);
            pImpl_->pCallChar_->notify();
            return true;
        }
    }
    return false;
}

bool Esp32Bluetooth::dial(const char* number) {
    DebugLog::log("BLE: Command -> DIAL '%s'", number ? number : "");
    if (number && number[0] && capabilities().dial && pImpl_->pCallChar_ && pImpl_->connected_) {
        char buf[32];
        buf[0] = 0x03; // Dial command
        if (number) {
            strncpy(buf + 1, number, sizeof(buf) - 2);
            buf[sizeof(buf) - 1] = '\0';
        } else {
            buf[1] = '\0';
        }
        pImpl_->pCallChar_->setValue(reinterpret_cast<uint8_t*>(buf), strlen(buf + 1) + 1);
        pImpl_->pCallChar_->notify();
        return true;
    }
    return false;
}

bool Esp32Bluetooth::mediaCommand(CompanionMediaAction action) {
    DebugLog::log("BLE: Command -> MEDIA ACTION %d", int(action));
    if (pImpl_) {
        if (capabilities().media && pImpl_->appleClient_.isAmsActive()) {
            pImpl_->appleClient_.mediaCommand(action);
            return true;
        }
        if (pImpl_->pMediaChar_ && pImpl_->connected_) {
            uint8_t val = static_cast<uint8_t>(action);
            pImpl_->pMediaChar_->setValue(&val, 1);
            pImpl_->pMediaChar_->notify();
            return true;
        }
    }
    return false;
}

bool Esp32Bluetooth::dismissNotification(uint32_t uid) {
    return pImpl_ && pImpl_->appleClient_.dismissNotification(uid);
}

} // namespace hal
} // namespace ersa

#else

// Host stub implementation for tests/desktop builds
namespace ersa {
namespace hal {

Esp32Bluetooth::Esp32Bluetooth() = default;
Esp32Bluetooth::~Esp32Bluetooth() = default;

Result<void> Esp32Bluetooth::init() { return Result<void>(); }
void Esp32Bluetooth::startAdvertising() {}
void Esp32Bluetooth::stopAdvertising() {}
bool Esp32Bluetooth::suspendForMaintenance() { stopAdvertising(); return true; }
void Esp32Bluetooth::resumeAfterMaintenance() { startAdvertising(); }
bool Esp32Bluetooth::isConnected() const { return false; }
bool Esp32Bluetooth::isAdvertising() const { return false; }
uint32_t Esp32Bluetooth::nextWakeDelayMs(uint32_t) const { return UINT32_MAX; }
void Esp32Bluetooth::tick() {}
void Esp32Bluetooth::beginAdvertising() {}
const char* Esp32Bluetooth::getDeviceName() const {
    const auto* board = ersa::board::Board::currentOrNull();
    return board ? board->getDeviceInfo().name : "Unknown Device";
}
const char* Esp32Bluetooth::getDeviceAddress() const { return "24:DC:C3:01:23:45"; }

const char* Esp32Bluetooth::sourceId() const { return "none"; }
bool Esp32Bluetooth::isAvailable() const { return false; }
CompanionCapabilities Esp32Bluetooth::capabilities() const { return {}; }
void Esp32Bluetooth::setCallCallback(CompanionCallCallback cb, void* userData) {
    callCb_ = cb;
    callUserData_ = userData;
}

void Esp32Bluetooth::setMediaCallback(CompanionMediaCallback cb, void* userData) {
    mediaCb_ = cb;
    mediaUserData_ = userData;
}

void Esp32Bluetooth::setConnectionCallback(BleConnectionCallback cb, void* userData) {
    connCb_ = cb;
    connUserData_ = userData;
}

void Esp32Bluetooth::setNotificationCallback(CompanionNotificationCallback cb, void* user) { notifCb_ = cb; notifUserData_ = user; }
void Esp32Bluetooth::setTimeCallback(CompanionTimeCallback, void*) {}
void Esp32Bluetooth::setAvailabilityCallback(CompanionAvailabilityCallback cb, void* user) {
    availabilityCb_ = cb; availabilityUserData_ = user;
}
bool Esp32Bluetooth::acceptCall() { return false; }
bool Esp32Bluetooth::rejectCall() { return false; }
bool Esp32Bluetooth::hangupCall() { return false; }
bool Esp32Bluetooth::dial(const char*) { return false; }
bool Esp32Bluetooth::mediaCommand(CompanionMediaAction) { return false; }
bool Esp32Bluetooth::dismissNotification(uint32_t) { return false; }

} // namespace hal
} // namespace ersa

#endif
