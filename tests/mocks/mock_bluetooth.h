#pragma once

#include "ersa/hal/bluetooth.h"
#include "ersa/hal/companion_source.h"
#include <string.h>

namespace ersa {
namespace test {

class MockBluetooth : public hal::IBluetooth, public hal::ICompanionSource {
public:
    MockBluetooth() = default;

    Result<void> init() override { return Result<void>(); }
    const char* sourceId() const override { return "test-mock"; }
    bool isAvailable() const override { return connected_; }
    hal::CompanionCapabilities capabilities() const override {
        hal::CompanionCapabilities c{};
        c.notifications = c.media = c.calls = c.answerReject = c.hangup = c.dial = c.remoteDismiss = connected_;
        return c;
    }
    void setCallCallback(hal::CompanionCallCallback cb, void* userData) override {
        callCb_ = cb; callUserData_ = userData;
    }
    void setMediaCallback(hal::CompanionMediaCallback cb, void* userData) override {
        mediaCb_ = cb; mediaUserData_ = userData;
    }
    void setNotificationCallback(hal::CompanionNotificationCallback cb, void* userData) override {
        notifCb_ = cb; notifUserData_ = userData;
    }
    void setTimeCallback(hal::CompanionTimeCallback cb, void* userData) override {
        timeCb_ = cb; timeUserData_ = userData;
    }
    void setAvailabilityCallback(hal::CompanionAvailabilityCallback cb, void* userData) override {
        availabilityCb_ = cb; availabilityUserData_ = userData;
    }
    bool acceptCall() override { ++acceptCount_; return connected_; }
    bool rejectCall() override { ++rejectCount_; return connected_; }
    bool hangupCall() override { ++hangupCount_; return connected_; }
    bool dial(const char* number) override {
        if (!connected_ || !number || !number[0]) return false;
        ++dialCount_; strncpy(lastDialed_, number, sizeof(lastDialed_) - 1);
        lastDialed_[sizeof(lastDialed_) - 1] = '\0'; return true;
    }
    bool mediaCommand(hal::CompanionMediaAction action) override {
        if (!connected_) return false;
        lastMediaAction_ = action; ++mediaCmdCount_; return true;
    }
    bool dismissNotification(uint32_t uid) override {
        if (!connected_) return false;
        ++notificationDismissCount_; lastDismissedUid_ = uid; return true;
    }
    void startAdvertising() override { advertising_ = true; }
    void stopAdvertising() override { advertising_ = false; }
    bool isConnected() const override { return connected_; }
    const char* getDeviceName() const override { return "Ersa Wearable"; }
    const char* getDeviceAddress() const override { return "AA:BB:CC:11:22:33"; }

    void setConnectionCallback(hal::BleConnectionCallback cb, void* userData) override {
        connCb_ = cb;
        connUserData_ = userData;
    }

    // Test helper simulation triggers
    void simulateConnection(bool conn) {
        connected_ = conn;
        if (connCb_) connCb_(conn, connUserData_);
        if (availabilityCb_) availabilityCb_(conn, availabilityUserData_);
    }

    void simulateIncomingCall(const char* caller, const char* number) {
        if (callCb_) callCb_(hal::CompanionCallAction::Incoming, caller, number, callUserData_);
    }

    void simulateCallAnswered() {
        if (callCb_) callCb_(hal::CompanionCallAction::Answered, "", "", callUserData_);
    }

    void simulateCallEnded() {
        if (callCb_) callCb_(hal::CompanionCallAction::Ended, "", "", callUserData_);
    }

    void simulateMedia(bool playing, const char* title, const char* artist) {
        if (mediaCb_) mediaCb_(playing, title, artist, mediaUserData_);
    }

    void simulateTime(uint32_t epoch) {
        if (timeCb_) timeCb_(epoch, timeUserData_);
    }

    void simulateNotification(const char* title, const char* message, const char* app = "Messages", uint32_t uid = 1, bool canDismissRemotely = false) {
        if (notifCb_) notifCb_(title, message, app, uid, canDismissRemotely, notifUserData_);
    }

    bool isAdvertising() const { return advertising_; }
    int acceptCount() const { return acceptCount_; }
    int rejectCount() const { return rejectCount_; }
    int hangupCount() const { return hangupCount_; }
    int dialCount() const { return dialCount_; }
    const char* lastDialed() const { return lastDialed_; }
    int mediaCmdCount() const { return mediaCmdCount_; }
    hal::CompanionMediaAction lastMediaAction() const { return lastMediaAction_; }
    int notificationDismissCount() const { return notificationDismissCount_; }
    uint32_t lastDismissedUid() const { return lastDismissedUid_; }

private:
    bool advertising_{false};
    bool connected_{false};

    hal::CompanionCallCallback callCb_{nullptr};
    void* callUserData_{nullptr};

    hal::CompanionMediaCallback mediaCb_{nullptr};
    void* mediaUserData_{nullptr};

    hal::BleConnectionCallback connCb_{nullptr};
    void* connUserData_{nullptr};

    hal::CompanionNotificationCallback notifCb_{nullptr};
    void* notifUserData_{nullptr};
    hal::CompanionTimeCallback timeCb_{nullptr};
    void* timeUserData_{nullptr};
    hal::CompanionAvailabilityCallback availabilityCb_{nullptr};
    void* availabilityUserData_{nullptr};

    int acceptCount_{0};
    int rejectCount_{0};
    int hangupCount_{0};
    int dialCount_{0};
    char lastDialed_[32]{""};

    int mediaCmdCount_{0};
    hal::CompanionMediaAction lastMediaAction_{hal::CompanionMediaAction::Play};
    int notificationDismissCount_{0};
    uint32_t lastDismissedUid_{0};
};

} // namespace test
} // namespace ersa
