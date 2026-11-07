// Theme packs off the SD card: the packs are registered once at boot; each pack's radar clip and alert sounds are read when
// its theme is selected.
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

// Call from the draw path with the theme on screen (cheap when there is nothing to do). Reads that pack's radar.vid and
// alert.snd from the card into PSRAM once the theme has stayed selected for packmedia::SETTLE_MS, and drops the previous
// pack's. Blocks the caller for the read (up to about 1.4 MB). `immediate` (boot) skips the wait. Does nothing for a
// built-in theme, a pack without those files, or while a pack's alert sound is playing.
void tick(uint8_t themeId, uint32_t nowMs, bool immediate = false);

// How many pack folders scan() skipped (each with a WARN in /diag.log).
// Saturates at 255.
uint8_t skipped();

} // namespace themeload
