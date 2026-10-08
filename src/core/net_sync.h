#pragma once
#include <stdint.h>
#include <stddef.h>
#include <RTClib.h>

namespace NetSync {

struct CalEvent {
    char title[32];
    char timeStr[20];
    uint32_t dayKey; // YYYYMMDD in the watch's local calendar
};

struct CalTodo {
    char title[36];
    bool completed;
    char uid[36];
};

constexpr size_t MAX_EVENTS_PER_DAY = 6;
constexpr size_t CALENDAR_CACHE_DAYS = 7;
constexpr size_t MAX_EVENTS = MAX_EVENTS_PER_DAY * CALENDAR_CACHE_DAYS;
constexpr size_t MAX_TODOS = 12;

void begin();
bool syncNtp();
bool syncAll();
/// Start a non-blocking boot fallback when no phone/manual time has arrived.
bool startBootTimeSync();
/// Apply any completed boot sync from the main/UI task.
void tick();

size_t eventCount();
const CalEvent& getEvent(size_t index);

size_t todoCount();
const CalTodo& getTodo(size_t index);
void toggleTodo(size_t index);

bool isSyncing();
const char* lastStatus();

} // namespace NetSync
