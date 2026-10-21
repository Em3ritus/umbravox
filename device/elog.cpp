// Device side of elog: turn an event into a line, mirror it to Serial, and
// append it to /diag.log on the card (rotating once at the cap). All SD access
// goes through sdlog so it shares the same mount and bus handling.
#include "elog.h"
#include "sdlog.h"
#include <Arduino.h>
#include <esp_system.h>      // esp_reset_reason
#include <stdarg.h>

namespace elog {
namespace {

const char* resetName(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_EXT:       return "EXT";
        case ESP_RST_SW:        return "SW";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_INT_WDT:   return "INT_WDT";
        case ESP_RST_TASK_WDT:  return "TASK_WDT";
        case ESP_RST_WDT:       return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "UNKNOWN";
    }
}

// A reset that is not a clean power-on / software restart means the previous
// run died -- that is the line worth noticing in the file.
bool abnormal(esp_reset_reason_t r) {
    return r == ESP_RST_PANIC || r == ESP_RST_BROWNOUT ||
           r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT;
}

void writeLine(Level lvl, const char* tag, const char* msg) {
    char line[256];
    const int len = format(line, sizeof line, millis(), lvl, tag, msg);
    if (len <= 0) return;
    Serial.print(line);                    // always mirror to the console
    if (!sdlog::present()) return;         // no card: Serial is all we have
    if (shouldRotate(sdlog::sizeOf(PATH), (uint32_t)len, CAP))
        sdlog::rotate(PATH, OLD);
    sdlog::append(PATH, (const uint8_t*)line, (uint32_t)len);
}

} // namespace

void event(Level lvl, const char* tag, const char* fmt, ...) {
    char msg[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    writeLine(lvl, tag, msg);
}

void begin() {
    const esp_reset_reason_t rr = esp_reset_reason();
    event(INFO, "boot", "umbravox build %s %s, %s rev%d",
          __DATE__, __TIME__, ESP.getChipModel(), ESP.getChipRevision());
    event(abnormal(rr) ? WARN : INFO, "boot",
          "reset=%s heap=%uKB psram=%uKB free",
          resetName(rr),
          (unsigned)(ESP.getFreeHeap() >> 10),
          (unsigned)(ESP.getFreePsram() >> 10));
}

void dump(Print& out) {
    if (!sdlog::present()) { out.println("(no card)"); return; }
    if (!sdlog::dump(out, PATH)) out.println("(no diag.log yet)");
}

} // namespace elog
