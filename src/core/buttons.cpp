#include "buttons.h"
#include "ersa/board/board.h"
#include "debug_log.h"
#include <Arduino.h>
#include <OneButton.h>

namespace {
constexpr uint8_t EVENT_CAPACITY = 16;
Buttons::Event events[EVENT_CAPACITY] = {};
uint8_t head = 0;
uint8_t tail = 0;

OneButton topButton;
OneButton bottomButton;

void push(Buttons::Event event) {
    const uint8_t next = (head + 1) % EVENT_CAPACITY;
    if (next == tail) {
        DebugLog::log("BUTTON queue full; dropping %s", Buttons::name(event));
        return;
    }
    events[head] = event;
    head = next;
    DebugLog::log("BUTTON event=%s", Buttons::name(event));
}

void onTopClick() { push(Buttons::Event::Next); }
void onTopLongPress() { push(Buttons::Event::Home); }
void onBottomClick() { push(Buttons::Event::Action); }
void onBottomLongPress() { push(Buttons::Event::ActionLong); }
} // namespace

const char* Buttons::name(Buttons::Event event) {
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

void Buttons::begin() {
    topButton.setup(ersa::board::Board::current().getPins().buttons().top.number, INPUT_PULLUP, true);
    bottomButton.setup(ersa::board::Board::current().getPins().buttons().bottom.number, INPUT_PULLUP, true);

    for (OneButton* button : {&topButton, &bottomButton}) {
        button->setDebounceMs(20);
        // Keep single clicks responsive. Double-click actions were never
        // connected by this firmware and would add a 250ms wait to every tap.
        button->setClickMs(15);
        button->setPressMs(450);
    }
    topButton.attachClick(onTopClick);
    topButton.attachLongPressStart(onTopLongPress);
    bottomButton.attachClick(onBottomClick);
    bottomButton.attachLongPressStart(onBottomLongPress);

    head = tail = 0;
    DebugLog::log("BUTTON: polling GPIO%d=%d GPIO%d=%d (LOW=pressed)",
                  ersa::board::Board::current().getPins().buttons().top.number, digitalRead(ersa::board::Board::current().getPins().buttons().top.number),
                  ersa::board::Board::current().getPins().buttons().bottom.number, digitalRead(ersa::board::Board::current().getPins().buttons().bottom.number));
}

void Buttons::tick() {
    static int previousTop = -1;
    static int previousBottom = -1;
    const int topLevel = digitalRead(ersa::board::Board::current().getPins().buttons().top.number);
    const int bottomLevel = digitalRead(ersa::board::Board::current().getPins().buttons().bottom.number);
    if (topLevel != previousTop || bottomLevel != previousBottom) {
        DebugLog::log("BUTTON raw GPIO%d=%d GPIO%d=%d (0=pressed)",
                      ersa::board::Board::current().getPins().buttons().top.number, topLevel, ersa::board::Board::current().getPins().buttons().bottom.number, bottomLevel);
        previousTop = topLevel;
        previousBottom = bottomLevel;
    }
    topButton.tick();
    bottomButton.tick();
}

Buttons::Event Buttons::takeEvent() {
    if (head == tail) return Event::None;
    const Event event = events[tail];
    tail = (tail + 1) % EVENT_CAPACITY;
    return event;
}

bool Buttons::isPressed() {
    return digitalRead(ersa::board::Board::current().getPins().buttons().top.number) == LOW || digitalRead(ersa::board::Board::current().getPins().buttons().bottom.number) == LOW;
}

bool Buttons::bothPressed() {
    return digitalRead(ersa::board::Board::current().getPins().buttons().top.number) == LOW && digitalRead(ersa::board::Board::current().getPins().buttons().bottom.number) == LOW;
}

bool Buttons::hasPendingEvents() {
    return head != tail;
}
