#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace ersa { namespace protocols {

/** Decode a four-byte little-endian integer from a validated input buffer. */
inline uint32_t readLe32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
/** Encode a 32-bit integer as four little-endian bytes into caller storage. */
inline void writeLe32(uint8_t* p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(value >> (8 * i));
}

/**
 * Transport-independent decoder for one ANCS notification-attribute response.
 *
 * The firmware issues at most one attribute request at a time, so this parser
 * accumulates its fragments in one bounded buffer and verifies the UID before
 * publishing text. It knows the ANCS wire format but not GATT callbacks; that
 * split makes malformed/truncated packet behavior testable without a radio.
 */
class AncsAttributes {
public:
    /** Response parser state returned by feed(). */
    enum class Result { More, Complete, Invalid };
    /**
     * Start collecting the response for `uid` and clear prior text.
     * @param uid Notification identifier expected in the next response.
     * @param includeNegativeAction Also parse the optional action label.
     */
    void begin(uint32_t uid, bool includeNegativeAction = false) {
        uid_ = uid; size_ = 0; includeNegativeAction_ = includeNegativeAction;
        title[0] = message[0] = negativeActionLabel[0] = 0;
    }
    /**
     * Append a response fragment and parse any complete requested attributes.
     * @return More while bytes are incomplete, Complete for an exact valid
     *         response, or Invalid for overflow, wrong UID/order, or extra data.
     */
    Result feed(const uint8_t* data, size_t length) {
        if (length > sizeof(bytes_) - size_) return Result::Invalid;
        memcpy(bytes_ + size_, data, length);
        size_ += length;
        if (size_ < 5) return Result::More;
        if (bytes_[0] != 0 || readLe32(bytes_ + 1) != uid_) return Result::Invalid;
        size_t pos = 5;
        const unsigned count = includeNegativeAction_ ? 3 : 2;
        for (unsigned i = 0; i < count; ++i) {
            if (size_ - pos < 3) return Result::More;
            const uint8_t id = bytes_[pos];
            const size_t len = bytes_[pos + 1] | (size_t(bytes_[pos + 2]) << 8);
            const uint8_t expectedId = i == 0 ? 1 : (i == 1 ? 3 : 7);
            const size_t capacity = i == 0 ? sizeof(title) : (i == 1 ? sizeof(message) : sizeof(negativeActionLabel));
            if (id != expectedId || len >= capacity) return Result::Invalid;
            pos += 3;
            if (size_ - pos < len) return Result::More;
            char* out = i == 0 ? title : (i == 1 ? message : negativeActionLabel);
            memcpy(out, bytes_ + pos, len);
            out[len] = 0;
            pos += len;
        }
        return pos == size_ ? Result::Complete : Result::Invalid;
    }
    /**
     * Encode the ANCS Get Notification Attributes command into caller storage.
     * The output needs 11 bytes for title/message and 12 when action label is
     * requested; caller supplies the correct capacity for the selected form.
     */
    static void request(uint32_t uid, bool includeNegativeAction, uint8_t* out) {
        out[0] = 0;
        writeLe32(out + 1, uid);
        out[5] = 1; out[6] = 31; out[7] = 0;
        out[8] = 3; out[9] = 63; out[10] = 0;
        if (includeNegativeAction) {
            // Action labels are variable-length attributes without a
            // requested-length parameter in the ANCS command format.
            out[11] = 7;
        }
    }
    /** Convenience overload for the standard fixed 11-byte request. */
    static void request(uint32_t uid, uint8_t (&out)[11]) {
        request(uid, false, out);
    }
    /**
     * Return whether the optional negative action label means dismiss/clear.
     * Whitespace and ASCII case are ignored to handle common provider variants.
     */
    bool negativeActionIsDismissal() const {
        char normalized[sizeof(negativeActionLabel)]{};
        size_t n = 0;
        for (const char* p = negativeActionLabel; *p && n + 1 < sizeof(normalized); ++p) {
            const char c = *p;
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
            normalized[n++] = (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
        }
        return strcmp(normalized, "dismiss") == 0 || strcmp(normalized, "clear") == 0;
    }
    char title[32]{};
    char message[64]{};
    char negativeActionLabel[32]{};
private:
    uint32_t uid_{0};
    bool includeNegativeAction_{false};
    uint8_t bytes_[140]{};
    size_t size_{0};
};

/**
 * Tracks an ANCS call notification and validates call actions against its UID.
 * The phone may remove a notification while the UI is open; checking the UID
 * and advertised action flags prevents stale controls from acting on another
 * call or sending unsupported actions.
 */
struct AncsCall {
    /** Notification UID that owns the current call action flags. */
    uint32_t uid{0};
    /** Action bits advertised by the remote notification. */
    uint8_t flags{0};
    /** Whether the tracked call notification is still present. */
    bool ringing{false};
    /** Record a new active call notification and its supported action bits. */
    void update(uint32_t id, uint8_t eventFlags) { uid = id; flags = eventFlags; ringing = true; }
    /** Remove the currently tracked call only when the UID matches. */
    bool remove(uint32_t id) {
        if (!ringing || id != uid) return false;
        ringing = false;
        return true;
    }
    /**
     * Encode a supported positive/negative action for the matching live UID.
     * @return False when stale, absent, or not advertised by the phone.
     */
    bool action(uint32_t id, bool positive, uint8_t (&out)[6]) const {
        if (!ringing || id != uid || !(flags & (positive ? 8 : 16))) return false;
        out[0] = 2; writeLe32(out + 1, uid); out[5] = positive ? 0 : 1;
        return true;
    }
};

} } // namespace ersa::protocols
