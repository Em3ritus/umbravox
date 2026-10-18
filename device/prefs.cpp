#include "prefs.h"
#include <Arduino.h>
#include <Preferences.h>
#include "nav.h"

namespace prefs {
namespace {

constexpr const char* NS  = "aether";
constexpr uint8_t     VER = 3;          // bump when the layout changes
constexpr uint32_t    SETTLE_MS = 2000;

Preferences   nvs;
bool          ready = false;
ui::Settings  stored;                   // what is believed to be on flash
ui::Settings  lastSeen;                 // value at the previous tick
uint32_t      steadyAt = 0;             // when it last stopped changing

void write(const ui::Settings& s) {
    if (!ready) return;
    nvs.putUChar("ver",     VER);
    nvs.putUChar("bright",  s.bright);
    nvs.putUShort("dwell",  s.dwellMs);
    nvs.putUChar("bleduty", s.bleDuty);
    nvs.putUChar("bands",   s.bands);
    nvs.putUChar("alertmin", s.alertMin);
    nvs.putUChar("idle",    s.idleIdx);
    stored = s;
    lastSeen = s;
}

} // namespace

bool load(ui::Settings& s) {
    ready = nvs.begin(NS, /*readOnly=*/false);
    if (!ready) {
        Serial.println("prefs: NVS unavailable, using defaults");
        return false;
    }

    const uint8_t ver = nvs.getUChar("ver", 0);
    if (ver != VER) {
        // Nothing stored, or stored by a layout this build does not know.
        // Defaults win and are written back, rather than guessing at a
        // migration for a format that has only ever had one version.
        Serial.printf("prefs: no usable store (ver %u), writing defaults\n", ver);
        write(s);
        lastSeen = s;
        return false;
    }

    ui::Settings in;
    in.bright  = nvs.getUChar("bright",  s.bright);
    in.dwellMs = nvs.getUShort("dwell",  s.dwellMs);
    in.bleDuty = nvs.getUChar("bleduty", s.bleDuty);
    in.bands   = nvs.getUChar("bands",   s.bands);
    in.alertMin = nvs.getUChar("alertmin", s.alertMin);
    in.idleIdx  = nvs.getUChar("idle",     s.idleIdx);

    // Anything out of range is treated as corrupt and pulled back in, not
    // trusted because it came off flash.
    ui::clampSettings(in);
    s = in;
    stored = in;
    lastSeen = in;

    Serial.printf("prefs: loaded bright %u%%, dwell %ums, ble %u%%, bands %u, alert %u\n",
                  in.bright, in.dwellMs, in.bleDuty, in.bands, in.alertMin);
    return true;
}

void tick(const ui::Settings& s, uint32_t nowMs) {
    if (!ready) return;

    // The timer keys off *changes*, not off differing from flash. Keying off
    // the difference from flash never fires: that difference persists until
    // the write, so the deadline is pushed back every tick and the write the
    // deadline was supposed to trigger never happens.
    if (!ui::settingsEqual(s, lastSeen)) {
        lastSeen = s;
        steadyAt = nowMs;
        return;
    }
    if (!ui::settingsEqual(s, stored) && (nowMs - steadyAt) >= SETTLE_MS) {
        write(s);
        Serial.println("prefs: saved");
    }
}

void flush(const ui::Settings& s) {
    if (!ui::settingsEqual(s, stored)) write(s);
}

} // namespace prefs
