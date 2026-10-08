#pragma once
#include <stdint.h>
#include <stddef.h>

namespace DebugLog {
/** Fixed-size record copied out of the bounded boot log ring. */
struct Record { uint32_t sequence; char text[240]; };
/** Load boot counter/reset metadata and initialize the in-memory log ring. */
void begin();
/** Append a formatted message; format is printf-style and size bounded. */
void log(const char* format, ...) __attribute__((format(printf, 1, 2)));
/** Flush pending serial/log output from the main task. */
void tick();
/** Enable framing/transport mode used by the USB control channel. */
void setProtocolMode(bool enabled);
// Copies oldest-to-newest records newer than cursor. Returns copied count and
// advances cursor to the newest sequence observed, including records skipped
// because the caller's buffer was smaller than the ring.
size_t readSince(uint32_t cursor, Record* out, size_t capacity, uint32_t* nextCursor);
/** Return the newest assigned ring sequence number. */
uint32_t latestSequence();
/** Return persisted boot count used by diagnostics. */
uint32_t bootCount();
/** Return a stable label for the current reset cause. */
const char* resetReasonName();
/** Drain buffered output before shutdown or reset. */
void flush();
}
