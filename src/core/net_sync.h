#pragma once
#include <stdint.h>
#include <stddef.h>
#include <RTClib.h>

namespace NetSync {

/** Cached calendar entry with a local-calendar day key for agenda filtering. */
struct CalEvent {
    /** Event label copied from parsed iCalendar content. */
    char title[32];
    /** Localized time string prepared for display. */
    char timeStr[20];
    /** YYYYMMDD key in the configured local calendar. */
    uint32_t dayKey;
};

/** Cached task entry retained when the device is offline. */
struct CalTodo {
    /** Task label presented in the todo app. */
    char title[36];
    /** Locally cached completion state. */
    bool completed;
    /** Provider UID used to associate this task with its calendar item. */
    char uid[36];
};

/** Maximum events retained for one local calendar day. */
constexpr size_t MAX_EVENTS_PER_DAY = 6;
/** Number of calendar days represented by the bounded cache. */
constexpr size_t CALENDAR_CACHE_DAYS = 7;
/** Total fixed event capacity across the cache window. */
constexpr size_t MAX_EVENTS = MAX_EVENTS_PER_DAY * CALENDAR_CACHE_DAYS;
/** Maximum task entries retained in RAM and persistent cache. */
constexpr size_t MAX_TODOS = 12;

/** Load cached items and initialize synchronization state. */
void begin();
/** Run a synchronous NTP/HTTP time sync for explicit foreground requests. */
bool syncNtp();
/** Run time and configured CalDAV synchronization for explicit user requests. */
bool syncAll();
/** Start a non-blocking boot fallback when no phone/manual time has arrived. */
bool startBootTimeSync();
/** Apply completed boot-sync results from the UI task, preserving source priority. */
void tick();

/** Return the number of currently retained calendar entries. */
size_t eventCount();
/** Return a cached event by index; callers must check eventCount() first. */
const CalEvent& getEvent(size_t index);

/** Return the number of currently retained task entries. */
size_t todoCount();
/** Return a cached task by index; callers must check todoCount() first. */
const CalTodo& getTodo(size_t index);
/** Toggle a task's local completion bit and persist the updated cache. */
void toggleTodo(size_t index);

/** Report whether a sync operation currently owns Wi-Fi resources. */
bool isSyncing();
/** Return a borrowed status string describing the latest sync outcome. */
const char* lastStatus();

} // namespace NetSync
