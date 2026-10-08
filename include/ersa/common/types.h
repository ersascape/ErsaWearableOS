#pragma once

#include <stdint.h>
#include <stddef.h>

namespace ersa {

/**
 * Stable error categories shared by host services and device HAL adapters.
 * Negative values distinguish failure from ordinary positive counts while
 * keeping the representation simple for embedded compilers and protocols.
 */
enum class ErrorCode : int {
    Ok = 0,
    InvalidParam = -1,
    OutOfMemory = -2,
    NotFound = -3,
    Busy = -4,
    Timeout = -5,
    NotSupported = -6,
    HardwareFault = -7,
    IoError = -8
};

/** Outcome code and optional diagnostic returned by a fallible operation. */
struct Error {
    /** Machine-readable result used for caller branching. */
    ErrorCode code{ErrorCode::Ok};
    /** Non-owning diagnostic string; implementations should use stable storage. */
    const char* message{""};

    /** Construct a successful outcome, which is the default state. */
    constexpr Error() = default;
    /** Construct an outcome from a category and optional diagnostic. */
    constexpr Error(ErrorCode c, const char* msg = "") : code(c), message(msg) {}

    /** Return true only for ErrorCode::Ok. */
    constexpr bool isOk() const { return code == ErrorCode::Ok; }
    /** Return true for every non-success category. */
    constexpr bool isError() const { return code != ErrorCode::Ok; }
};

/**
 * Value-or-error return type for HAL and service operations.
 *
 * This keeps hardware and host implementations on one exception-free contract.
 * A successful instance carries a value; a failed instance carries an Error.
 * Callers should inspect isOk() before reading value(), because failed results
 * retain only a default-constructed value for deterministic storage.
 */
template <typename T>
class Result {
public:
    /** Create a successful result by copying `val`. */
    Result(const T& val) : value_(val), error_(ErrorCode::Ok) {}
    /** Create a successful result by moving `val` into the result. */
    Result(T&& val) : value_(static_cast<T&&>(val)), error_(ErrorCode::Ok) {}
    /** Create a failed result from an Error value. */
    Result(const Error& err) : error_(err) {}
    /** Create a failed result from a code and optional diagnostic. */
    Result(ErrorCode code, const char* msg = "") : error_(code, msg) {}

    /** Test whether the operation completed successfully. */
    bool isOk() const { return error_.isOk(); }
    /** Test whether the operation completed with an error. */
    bool isError() const { return error_.isError(); }

    /** Read the returned value; valid only when isOk() is true. */
    const T& value() const { return value_; }
    /** Mutate the returned value; valid only when isOk() is true. */
    T& value() { return value_; }

    /** Inspect the outcome code and diagnostic for success or failure. */
    const Error& error() const { return error_; }

private:
    T value_{};
    Error error_{ErrorCode::Ok};
};

/** No-payload specialization for operations that report success or failure only. */
template <>
class Result<void> {
public:
    /** Construct a successful no-payload result. */
    Result() : error_(ErrorCode::Ok) {}
    /** Construct a failed no-payload result from an Error value. */
    Result(const Error& err) : error_(err) {}
    /** Construct a failed no-payload result from a code and diagnostic. */
    Result(ErrorCode code, const char* msg = "") : error_(code, msg) {}

    /** Test whether the operation completed successfully. */
    bool isOk() const { return error_.isOk(); }
    /** Test whether the operation completed with an error. */
    bool isError() const { return error_.isError(); }
    /** Inspect the outcome code and diagnostic. */
    const Error& error() const { return error_; }

private:
    Error error_{ErrorCode::Ok};
};

/** Logical pixel coordinate whose origin is the display's top-left corner. */
struct Point {
    /** Horizontal position increasing toward the right edge. */
    int16_t x{0};
    /** Vertical position increasing toward the bottom edge. */
    int16_t y{0};

    /** Construct the origin point. */
    constexpr Point() = default;
    /** Construct a point at the supplied coordinates. */
    constexpr Point(int16_t px, int16_t py) : x(px), y(py) {}
};

/**
 * Half-open axis-aligned rectangle used for layout and partial refresh regions.
 * Left/top are included and right/bottom are excluded; this convention matches
 * framebuffer indexing and allows adjacent regions to share an edge safely.
 */
struct Rect {
    /** Inclusive left edge in logical pixels. */
    int16_t x{0};
    /** Inclusive top edge in logical pixels. */
    int16_t y{0};
    /** Horizontal extent; non-positive values make the rectangle empty. */
    int16_t w{0};
    /** Vertical extent; non-positive values make the rectangle empty. */
    int16_t h{0};

    /** Construct an empty rectangle at the origin. */
    constexpr Rect() = default;
    /** Construct a rectangle with origin and width/height extents. */
    constexpr Rect(int16_t rx, int16_t ry, int16_t rw, int16_t rh)
        : x(rx), y(ry), w(rw), h(rh) {}

    /** Return true if either extent is zero or negative. */
    constexpr bool isEmpty() const { return w <= 0 || h <= 0; }
    /** Test point containment using the half-open edge convention. */
    constexpr bool contains(int16_t px, int16_t py) const {
        return px >= x && px < (x + w) && py >= y && py < (y + h);
    }
};

} // namespace ersa
