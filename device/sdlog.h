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
bool writeAtomic(const char* name,
                 uint32_t (*body)(Print&, uint32_t&),
                 uint32_t& rows, uint32_t& skipped);

} // namespace sdlog
