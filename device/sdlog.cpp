#include "sdlog.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

namespace sdlog {
namespace {
bool mounted = false;

// CONFIRM AGAINST THE BOARD. The NM-CYD-C5's SD chip-select is not documented
// publicly; PIN_CS (23) is the LCD's, not this. SCK/MOSI/MISO (6/7/2) are the
// shared bus. This value is a placeholder to be set from the schematic before
// the card is expected to mount -- see the Hardware Acceptance section.
constexpr int SD_CS = -1;
}

bool begin() {
    // The panel and touch already own the SPI bus; SD shares it. The exact
    // handoff (SPI.begin pins, SD.begin(cs, SPI)) is settled on hardware.
    mounted = (SD_CS >= 0) && SD.begin(SD_CS, SPI);
    return mounted;
}

bool present() { return mounted; }

bool writeAtomic(const char* name,
                 uint32_t (*body)(Print&, uint32_t&),
                 uint32_t& rows, uint32_t& skipped) {
    if (!mounted) return false;
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%s.tmp", name);

    SD.remove(tmp);
    File f = SD.open(tmp, FILE_WRITE);
    if (!f) return false;

    rows = body(f, skipped);
    f.flush();
    f.close();

    // Rename only after a clean close. SD.rename replaces the directory entry,
    // so "name" is either the old complete file or the new one, never a
    // fragment. Remove the old target first; FAT rename will not overwrite.
    SD.remove(name);
    if (!SD.rename(tmp, name)) { SD.remove(tmp); return false; }
    return true;
}

} // namespace sdlog
