#pragma once

#include "ersa/hal/console.h"
#include <string>

namespace ersa::test {

/** In-memory console for validating the byte-oriented platform contract. */
class MockConsole final : public hal::IConsole {
public:
    void begin(uint32_t baudRate) override { baudRate_ = baudRate; }
    bool isAttached() const override { return attached_; }
    size_t availableForWrite() const override { return outputCapacity_; }
    size_t available() const override { return input_.size() - inputOffset_; }
    int read() override {
        return inputOffset_ < input_.size() ? static_cast<uint8_t>(input_[inputOffset_++]) : -1;
    }
    size_t write(const uint8_t* data, size_t length) override {
        if (!data) return 0;
        const size_t count = length < outputCapacity_ ? length : outputCapacity_;
        output_.append(reinterpret_cast<const char*>(data), count);
        outputCapacity_ -= count;
        return count;
    }
    void flush() override { ++flushCount_; }

    bool attached_{true};
    size_t outputCapacity_{256};
    std::string input_;
    size_t inputOffset_{0};
    std::string output_;
    uint32_t baudRate_{0};
    uint32_t flushCount_{0};
};

} // namespace ersa::test
