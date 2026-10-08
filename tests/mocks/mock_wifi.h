#pragma once

#include "ersa/hal/wifi.h"
#include <cstdio>

namespace ersa {
namespace test {

class MockWifi final : public hal::IWifiRadio {
public:
    Result<void> init() override { state_ = hal::WifiState::Off; return Result<void>(); }
    void enableStation() override { state_ = hal::WifiState::Connecting; ++enableCalls; }
    void connectStation(const char*, const char*, bool) override { state_ = hal::WifiState::Connecting; }
    hal::WifiState state() const override { return state_; }
    bool copyLocalAddress(char* output, size_t capacity) const override {
        if (!output || capacity < 10) return false;
        std::snprintf(output, capacity, "%s", "127.0.0.1");
        return true;
    }
    Result<void> startAccessPoint(const char*, const char*) override {
        state_ = hal::WifiState::AccessPoint;
        return Result<void>();
    }
    void stopAccessPoint() override { state_ = hal::WifiState::Off; }
    bool copyAccessPointAddress(char* output, size_t capacity) const override {
        if (!output || capacity < 10) return false;
        std::snprintf(output, capacity, "%s", "192.168.4.1");
        return true;
    }
    void disconnect(bool powerOff) override {
        state_ = powerOff ? hal::WifiState::Off : hal::WifiState::Disconnected;
        ++disconnectCalls;
    }

    uint32_t enableCalls{0};
    uint32_t disconnectCalls{0};

private:
    hal::WifiState state_{hal::WifiState::Off};
};

} // namespace test
} // namespace ersa
