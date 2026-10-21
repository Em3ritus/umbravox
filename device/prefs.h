// Settings persistence, in NVS.
//
// Writes are debounced rather than immediate. Each tap on a +/- control
// changes a value, and a drive spent dialling brightness would otherwise be
// hundreds of flash writes -- NVS wear-levels, but there is no reason to
// spend the cycles. A save lands a couple of seconds after you stop touching
// it, which is indistinguishable from instant and costs one write.
#pragma once
#include <stdint.h>
#include "model.h"

namespace prefs {

// Load stored settings over `s`, leaving its defaults in place if nothing
// valid is stored. Returns true if something was loaded.
bool load(ui::Settings& s);

// Call every loop. Saves if `s` has differed from what is stored for long
// enough.
void tick(const ui::Settings& s, uint32_t nowMs);

// Write immediately, whatever the timer says.
void flush(const ui::Settings& s);

// The wardrive "currently logging" flag. Kept outside Settings and written
// immediately, not on the debounced timer, so a brownout a second after start
// still resumes logging on boot.
void setLogging(bool on);
bool loggingWas();

// The mesh network id (see device/mesh.h). Kept outside Settings and written
// immediately, like the logging flag: an enrolment must survive the very next
// reboot. netId() returns the stored id, or `deflt` (the build default) if none
// is stored yet.
void    setNetId(uint8_t id);
uint8_t netId(uint8_t deflt);

} // namespace prefs
