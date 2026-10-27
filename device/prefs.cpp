#include "prefs.h"
#include <Arduino.h>
#include <Preferences.h>
#include "nav.h"
#include "palette.h"
#include "themepack.h"

namespace prefs {
namespace {

constexpr const char* NS  = "aether";
constexpr uint8_t     VER = 4;          // bump when the layout changes
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
    nvs.putUChar("idlemode", s.idleMode);
    // Every field settingsEqual compares must also be stored here and read in
    // load(), or the save timer never settles and hammers flash. Anyone who
    // takes a spare byte for a new setting must add it to both.
    nvs.putUChar("theme",   s.theme);
    // A pack is saved by NAME as well: its id is only its slot in this boot's
    // load order, which adding or removing another pack shifts. Cleared (an
    // empty string, which load() reads as "no pack") for a built-in.
    nvs.putString("pack", s.theme >= pal::TH_COUNT ? pal::themeName(s.theme) : "");
    nvs.putUChar("sweep",   s.sweep);
    nvs.putUChar("labels",  s.labels);
    nvs.putUChar("alertsty", s.alertStyle);
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
    in.idleMode = nvs.getUChar("idlemode", s.idleMode);   // absent on older stores -> DIM
    in.theme    = nvs.getUChar("theme",    s.theme);
    // The saved pack name, if any, decides the theme (see write()). Absent or
    // too long for any real pack name reads back as "" -- no pack. Resolved
    // BEFORE clamping, so a stale pack id is never trusted on its own.
    char pack[themepack::MAX_NAME + 1] = {};
    if (nvs.isKey("pack")) nvs.getString("pack", pack, sizeof pack);
    pack[sizeof pack - 1] = 0;
    in.theme    = pal::resolveSaved(pack, in.theme);
    if (pack[0] && in.theme == pal::TH_UMBRAVOX)
        Serial.printf("prefs: saved theme pack \"%s\" not loaded, using UMBRAVOX\n", pack);
    in.sweep    = nvs.getUChar("sweep",    s.sweep);
    in.labels   = nvs.getUChar("labels",   s.labels);
    in.alertStyle = nvs.getUChar("alertsty", s.alertStyle);

    // Anything out of range is treated as corrupt and pulled back in, not
    // trusted because it came off flash.
    ui::clampSettings(in);
    s = in;
    stored = in;
    lastSeen = in;

    Serial.printf("prefs: loaded bright %u%%, dwell %ums, ble %u%%, bands %u, alert %u, theme %u (%s), sweep %u\n",
                  in.bright, in.dwellMs, in.bleDuty, in.bands, in.alertMin,
                  in.theme, pal::themeName(in.theme), in.sweep);
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

void setLogging(bool on) {
    if (!ready) return;
    nvs.putUChar("logging", on ? 1 : 0);   // immediate: a brownout one second
                                           // after start must still resume
}

bool loggingWas() {
    return ready && nvs.getUChar("logging", 0) != 0;
}

void setNetId(uint8_t id) {
    if (!ready) return;
    nvs.putUChar("netid", id);          // immediate: an enrolment must survive a reboot
}

uint8_t netId(uint8_t deflt) {
    return ready ? nvs.getUChar("netid", deflt) : deflt;
}

} // namespace prefs
