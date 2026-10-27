// Theme packs off the SD card: read once at boot, into the palette.
//
// Every decision (which folders count, in what order, whether a pack is
// admitted, whether a reset while loading one means skipping it) is made by
// the pure functions in ui/themepack.h, which are host-tested; this file only
// does the SD and NVS I/O around them.
#pragma once
#include <stdint.h>

namespace themeload {

// Read /themes/*/ and register every pack that passes (at most
// themepack::MAX_PACKS) with pal::addPack. Call ONCE, in setup(), after
// sdlog::begin() and elog::begin(), and before prefs::load() so a saved pack
// name resolves. With no card it loads nothing and skips nothing.
void scan();

// How many pack folders scan() skipped (each with a WARN in /diag.log).
// Saturates at 255.
uint8_t skipped();

} // namespace themeload
