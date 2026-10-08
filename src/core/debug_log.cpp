#include "debug_log.h"
#include "ersa/board/board.h"
#include "ersa/services/storage_service.h"
#include "watch_clock.h"
#include <stdarg.h>
#include <string.h>
#include <freertos/FreeRTOS.h>

// The selected console HAL must use USB Serial/JTAG, never UART0 on EPD GPIO20/21.
namespace {
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 10000;
// Keep a short actionable history without reserving excessive RAM for logs.
constexpr size_t LOG_RING_CAPACITY = 8;
DebugLog::Record logRing[LOG_RING_CAPACITY] = {};
uint32_t nextLogSequence = 1;
bool protocolMode = false;
portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;

// One compact NVS write per boot, never once per heartbeat/refresh. Keep the
// previous causes so reopening USB (which can itself reset the board) does
// not erase evidence of the preceding battery reset.
struct BootHistory {
    uint32_t magic;
    uint32_t count;
    uint32_t causes[4]; // newest first
};
BootHistory history = {};
bool historySaved = false;

bool isRoutineLog(const char* format) {
    // These fire continuously during normal use and drown out useful events.
    static constexpr const char* QUIET_PREFIXES[] = {
        "LOOP boot=", "BUTTON raw ",
    };
    for (const char* prefix : QUIET_PREFIXES) {
        if (strncmp(format, prefix, strlen(prefix)) == 0) return true;
    }
    return false;
}

void recordBoot() {
    constexpr uint32_t magic = 0x57415431;
    auto& storage = ersa::services::StorageService::instance();
    const bool opened = storage.init().isOk();
    if (opened && storage.getBytesLength("diag_boots") == sizeof(history))
        storage.getBytes("diag_boots", &history, sizeof(history));
    if (history.magic != magic) history = {magic, 0, {0, 0, 0, 0}};
    ++history.count;
    for (unsigned i = 3; i > 0; --i) history.causes[i] = history.causes[i - 1];
    history.causes[0] = ersa::board::Board::current().getDiagnostics().resetReasonCode();
    if (opened) {
        historySaved = storage.setBytes("diag_boots", &history, sizeof(history));
    }
    // Sudden power loss before this write completes may leave the preceding
    // record intact. This is diagnostic evidence, not a complete crash dump.
}
}

void DebugLog::begin() {
    recordBoot();
    ersa::board::Board::current().getConsole().begin(115200);
    // Do not block startup waiting for a USB host; the watch runs untethered.
}

void DebugLog::log(const char* format, ...) {
    if (!format || isRoutineLog(format)) return;
    char text[240];
    const int prefix = snprintf(text, sizeof(text), "[%lu] ",
        (unsigned long)ersa::board::Board::current().getUptimeMs());
    va_list args;
    va_start(args, format);
    vsnprintf(text + prefix, sizeof(text) - prefix - 2, format, args);
    va_end(args);
    size_t length = strlen(text);
    text[length++] = '\n';
    portENTER_CRITICAL(&logMux);
    Record& record = logRing[(nextLogSequence - 1) % LOG_RING_CAPACITY];
    record.sequence = nextLogSequence++;
    memcpy(record.text, text, length);
    record.text[length] = '\0';
    const bool machineProtocol = protocolMode;
    portEXIT_CRITICAL(&logMux);
    auto& console = ersa::board::Board::current().getConsole();
    if (!console.isAttached() || machineProtocol) return;
    // Drop a line rather than block buttons/display if the host stops reading.
    if (console.availableForWrite() >= length)
        console.write(reinterpret_cast<const uint8_t*>(text), length);
}

void DebugLog::setProtocolMode(bool enabled) {
    portENTER_CRITICAL(&logMux);
    protocolMode = enabled;
    portEXIT_CRITICAL(&logMux);
}

size_t DebugLog::readSince(uint32_t cursor, Record* out, size_t capacity, uint32_t* nextCursor) {
    if (!out || !capacity) return 0;
    portENTER_CRITICAL(&logMux);
    const uint32_t latest = nextLogSequence - 1;
    const uint32_t earliest = latest >= LOG_RING_CAPACITY ? latest - LOG_RING_CAPACITY + 1 : 1;
    uint32_t first = cursor + 1;
    if (first < earliest) first = earliest;
    size_t count = 0;
    for (uint32_t sequence = first; sequence <= latest && count < capacity; ++sequence) {
        const Record& record = logRing[(sequence - 1) % LOG_RING_CAPACITY];
        if (record.sequence == sequence) out[count++] = record;
    }
    if (nextCursor) *nextCursor = count ? out[count - 1].sequence : cursor;
    portEXIT_CRITICAL(&logMux);
    return count;
}

uint32_t DebugLog::latestSequence() {
    portENTER_CRITICAL(&logMux);
    const uint32_t latest = nextLogSequence - 1;
    portEXIT_CRITICAL(&logMux);
    return latest;
}

void DebugLog::tick() {
    static bool attached = false;
    static uint32_t lastReport = 0;
    const bool connected = ersa::board::Board::current().getConsole().isAttached();
    if (!connected) setProtocolMode(false);
    if (connected && !attached) {
        log("ErsaWearable boot=%lu reset=%s(%d) saved=%d; display shows HH:MM only",
            (unsigned long)history.count, resetReasonName(),
            int(ersa::board::Board::current().getDiagnostics().resetReasonCode()), historySaved);
        log("RESET history newest->oldest: %s, %s, %s, %s",
            ersa::board::Board::current().getDiagnostics().resetReasonName(history.causes[0]),
            ersa::board::Board::current().getDiagnostics().resetReasonName(history.causes[1]),
            ersa::board::Board::current().getDiagnostics().resetReasonName(history.causes[2]),
            ersa::board::Board::current().getDiagnostics().resetReasonName(history.causes[3]));
        log("PCB pins: upper S2/B1=GPIO%d lower S1/B2=GPIO%d; LOW=pressed",
            ersa::board::Board::current().getPins().buttons().top.number, ersa::board::Board::current().getPins().buttons().bottom.number);
    }
    attached = connected;
    const uint32_t now = ersa::board::Board::current().getUptimeMs();
    if (uint32_t(now - lastReport) < HEARTBEAT_INTERVAL_MS) return;
    lastReport = now;
    const DateTime time = WatchClock::now();
    auto& board = ersa::board::Board::current();
    const bool button1Pressed = board.getInput().isPressed(ersa::events::ButtonId::Button1);
    const bool button2Pressed = board.getInput().isPressed(ersa::events::ButtonId::Button2);
    log("LOOP boot=%lu time=%02u:%02u:%02u rtc=%s B1=%d B2=%d EPD_BUSY=%d heap=%u",
        (unsigned long)history.count,
        unsigned(time.hour()), unsigned(time.minute()), unsigned(time.second()),
        WatchClock::healthy() ? "online" : "offline",
        button1Pressed, button2Pressed, board.getDisplay().isBusy(),
        unsigned(board.getDiagnostics().freeHeapBytes()));
}

void DebugLog::flush() {
    ersa::board::Board::current().getConsole().flush();
}

uint32_t DebugLog::bootCount() { return history.count; }
const char* DebugLog::resetReasonName() {
    auto& diagnostics = ersa::board::Board::current().getDiagnostics();
    return diagnostics.resetReasonName(diagnostics.resetReasonCode());
}
