#if defined(ARDUINO)

#include "core/buttons.h"
#include "core/debug_log.h"
#include <Arduino.h>
#include <OneButton.h>

/** ESP32-backed compatibility event queue used by Esp32Input. */
namespace Buttons {
namespace {
constexpr uint8_t EVENT_CAPACITY = 16;
Event events[EVENT_CAPACITY] = {};
uint8_t head = 0;
uint8_t tail = 0;
int topPin = -1;
int bottomPin = -1;
bool topActiveLow = true;
bool bottomActiveLow = true;
OneButton topButton;
OneButton bottomButton;

void push(Event event) {
    const uint8_t next = (head + 1) % EVENT_CAPACITY;
    if (next == tail) {
        DebugLog::log("BUTTON queue full; dropping %s", name(event));
        return;
    }
    events[head] = event;
    head = next;
    DebugLog::log("BUTTON event=%s", name(event));
}

void onTopClick() { push(Event::Next); }
void onTopDoubleClick() { push(Event::Previous); }
void onTopLongPress() { push(Event::Home); }
void onBottomClick() { push(Event::Action); }
void onBottomDoubleClick() { push(Event::ActionAlt); }
void onBottomLongPress() { push(Event::ActionLong); }
} // namespace

const char* name(Event event) {
    switch (event) {
        case Event::Next: return "NEXT";
        case Event::Previous: return "PREVIOUS";
        case Event::Home: return "HOME";
        case Event::Action: return "ACTION";
        case Event::ActionAlt: return "ACTION_ALT";
        case Event::ActionLong: return "ACTION_LONG";
        default: return "NONE";
    }
}

uint8_t pinModeFor(Pull pull) {
    switch (pull) {
        case Pull::Up: return INPUT_PULLUP;
        case Pull::Down: return INPUT_PULLDOWN;
        case Pull::None: default: return INPUT;
    }
}

void begin(int topPinNumber, int bottomPinNumber, Pull topPull, Pull bottomPull,
           bool topIsActiveLow, bool bottomIsActiveLow) {
    topPin = topPinNumber;
    bottomPin = bottomPinNumber;
    topActiveLow = topIsActiveLow;
    bottomActiveLow = bottomIsActiveLow;
    topButton.setup(topPin, pinModeFor(topPull), topActiveLow);
    bottomButton.setup(bottomPin, pinModeFor(bottomPull), bottomActiveLow);
    for (OneButton* button : {&topButton, &bottomButton}) {
        button->setDebounceMs(20);
        // Give a second tap enough time to form a reliable double-click.
        button->setClickMs(250);
        button->setPressMs(450);
    }
    topButton.attachClick(onTopClick);
    topButton.attachDoubleClick(onTopDoubleClick);
    topButton.attachLongPressStart(onTopLongPress);
    bottomButton.attachClick(onBottomClick);
    bottomButton.attachDoubleClick(onBottomDoubleClick);
    bottomButton.attachLongPressStart(onBottomLongPress);
    head = tail = 0;
    DebugLog::log("BUTTON: input initialized GPIO%d=%d GPIO%d=%d (LOW=pressed)",
                  topPin, digitalRead(topPin), bottomPin, digitalRead(bottomPin));
}

void tick() {
    static int previousTop = -1;
    static int previousBottom = -1;
    const int topLevel = digitalRead(topPin);
    const int bottomLevel = digitalRead(bottomPin);
    if (topLevel != previousTop || bottomLevel != previousBottom) {
        DebugLog::log("BUTTON raw GPIO%d=%d GPIO%d=%d (0=pressed)",
                      topPin, topLevel, bottomPin, bottomLevel);
        previousTop = topLevel;
        previousBottom = bottomLevel;
    }
    topButton.tick();
    bottomButton.tick();
}

Event takeEvent() {
    if (head == tail) return Event::None;
    const Event event = events[tail];
    tail = (tail + 1) % EVENT_CAPACITY;
    return event;
}

bool hasPendingEvents() { return head != tail; }

} // namespace Buttons

#endif // ARDUINO
