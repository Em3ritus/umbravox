#include "sdlog.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

namespace sdlog {
namespace {
bool mounted = false;

// SD chip-select, confirmed against RockBase's NM-CYD-C5 schematic (see
// README: "SD CS 10"). PIN_CS (23) is the LCD's, touch is CS 1; SCK/MOSI/MISO
// (6/7/2) are the shared bus the SD joins on CS 10.
constexpr int SD_CS = 10;
}

bool begin() {
    // touch::begin() has already brought up the shared SPI bus (SCK 6 / MISO 2
    // / MOSI 7) on the global SPI instance, and the panel shares it under the
    // HAL lock. SD joins the same bus on CS 10; SD.begin reuses the already-
    // begun SPI and drives CS 10 itself, holding it high between transactions.
    mounted = (SD_CS >= 0) && SD.begin(SD_CS, SPI);
    if (mounted) {
        Serial.printf("sdlog: card mounted on CS %d, %llu MB\n",
                      SD_CS, (unsigned long long)(SD.cardSize() >> 20));
    } else {
        Serial.printf("sdlog: no card / mount failed on CS %d\n", SD_CS);
    }
    return mounted;
}

bool present() { return mounted; }

bool writeAtomic(const char* name,
                 uint32_t (*body)(Print&, uint32_t&),
                 uint32_t& rows, uint32_t& skipped) {
    if (!mounted || !name || !name[0]) return false;

    // name.tmp / name.bak alongside the target. Guard the buffer: a silent
    // snprintf truncation could make tmp == name and destroy the live file.
    char tmp[64], bak[64];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", name) >= (int)sizeof tmp) return false;
    if (snprintf(bak, sizeof bak, "%s.bak", name) >= (int)sizeof bak) return false;

    // 1. Write the whole document to a temp file. The live file is not touched
    //    at all until this has run and closed cleanly, so any failure here
    //    leaves the previous good file exactly as it was.
    SD.remove(tmp);                       // clear a stale temp from a prior crash
    File f = SD.open(tmp, FILE_WRITE);
    if (!f) return false;

    rows = body(f, skipped);
    f.flush();
    // Best-effort write-failure check (full card / I/O error). getWriteError may
    // be inert on some cores, so also require size()>0 (body always writes the
    // header). This runs BEFORE the live file is touched, so a caught failure
    // cannot corrupt it. An undetected truncated-but-closed write is the
    // inherent-to-FAT residual pinned by the Hardware Acceptance pulled-power test.
    const bool writeOk = (f.getWriteError() == 0) && (f.size() > 0);
    f.close();
    if (!writeOk) { SD.remove(tmp); return false; }

    // 2. Commit. FatFs f_rename fails if the destination exists, so the old file
    //    must move aside first -- but it is never deleted before the new one is
    //    safely in place. Move old -> bak, then tmp -> name; restore bak on fail.
    const bool hadOld = SD.exists(name);
    if (hadOld) {
        SD.remove(bak);                                 // clear a stale backup
        if (!SD.rename(name, bak)) { SD.remove(tmp); return false; }  // old file intact
    }
    if (!SD.rename(tmp, name)) {
        if (hadOld) SD.rename(bak, name);               // restore the old file
        SD.remove(tmp);
        return false;
    }
    if (hadOld) SD.remove(bak);                         // committed; drop the backup
    return true;
}

} // namespace sdlog
