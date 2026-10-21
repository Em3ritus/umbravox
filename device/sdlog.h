// The SD card: mount, and write a document crash-safely. Knows nothing of
// WiGLE -- it only puts a document on the card without a half-written file
// surviving. logging owns the session name and the bookkeeping.
#pragma once
#include <stdint.h>
class Print;

namespace sdlog {

bool begin();     // mount on the shared SPI bus; false and stays false if none
bool present();   // a card is mounted

// Write `body` to `name`, atomically: open "name.tmp", run body into it, flush,
// close, rename over "name". Returns true only if the rename succeeded, so a
// failure anywhere leaves the previous good "name" untouched.
// `name` must be an absolute path (a leading '/'), as SD.open requires.
// `rows` and `skipped` are meaningful only when this returns true.
bool writeAtomic(const char* name,
                 uint32_t (*body)(Print&, uint32_t&),
                 uint32_t& rows, uint32_t& skipped);

// Bring-up diagnostics (serial console). list() prints each root file as
// "<size>  <name>"; dump() streams one file's bytes to `out` and returns
// false if the card is absent or the file does not exist.
void list(Print& out);
bool dump(Print& out, const char* name);

// Highest N among root files named "<prefix>NNNN<suffix>", or 0 if none.
// `logging` uses it to advance the dateless session counter past files
// already on the card, so a reboot before a GPS fix can't overwrite one.
uint32_t maxIndex(const char* prefix, const char* suffix);

// Append-mode helpers for the diagnostic log (elog). Unlike writeAtomic these
// grow a file in place; the caller (elog) owns rotation so it never fills the
// card. All are best-effort and no-ops when no card is mounted.
uint32_t sizeOf(const char* name);                                  // bytes, or 0
bool     append(const char* name, const uint8_t* data, uint32_t len);
bool     rotate(const char* name, const char* to);                  // to <- name

// The longest a write held up the loop (ms) since this was last called, across
// writeAtomic and append. Reading it clears it; perflog logs it so the cost of
// the SD card itself is visible next to the frame times it competes with.
uint32_t takeMaxStallMs();

} // namespace sdlog
