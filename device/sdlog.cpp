#include "sdlog.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <unistd.h>

namespace sdlog {
namespace {
bool mounted = false;
uint32_t stallMax = 0;                         // longest write since takeMaxStallMs()
inline void noteStall(uint32_t t0) {
    const uint32_t d = millis() - t0;
    if (d > stallMax) stallMax = d;
}

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

// --- the session file: one append open at a time ---------------------------
namespace {
File     appF;
bool     appOk = false;
uint32_t appT0 = 0, appBytes = 0;
}

bool exists(const char* name) {
    return mounted && name && name[0] && SD.exists(name);
}

bool openAppend(const char* name, uint32_t& size) {
    if (!mounted || !name || !name[0]) return false;
    if (appF) appF.close();                 // never two at once
    appT0 = millis();
    appF = SD.open(name, FILE_APPEND);      // created if missing
    if (!appF) { noteStall(appT0); return false; }
    size = (uint32_t)appF.size();           // its length before this append
    appOk = true;
    appBytes = 0;
    return true;
}

bool writeAppend(const char* data, uint32_t len) {
    if (!appF || !data) return false;
    const size_t w = appF.write((const uint8_t*)data, len);
    if (w != len) appOk = false;
    appBytes += (uint32_t)w;
    return w == len;
}

bool closeAppend() {
    if (!appF) return false;
    // flush() is fflush + fsync: the directory entry's size is committed here,
    // so a power cut before it leaves the file at its previous (whole-line)
    // length under FatFs semantics.
    appF.flush();
    const bool ok = appOk && appF.getWriteError() == 0;
    appF.close();
    const uint32_t ms = millis() - appT0;
    noteStall(appT0);
    // Acceptance item #7: the per-save stall, now proportional to the batch.
    Serial.printf("sdlog: append %lu bytes in %lu ms%s\n", (unsigned long)appBytes,
                  (unsigned long)ms, ok ? "" : " -- FAILED");
    return ok;
}

bool truncateTo(const char* name, uint32_t len) {
    if (!mounted || !name || !name[0]) return false;
    // Arduino's File has no truncate; the VFS path does (FatFs f_truncate).
    char path[72];
    if (snprintf(path, sizeof path, "%s%s", SD.mountpoint(), name) >= (int)sizeof path)
        return false;
    const uint32_t t0 = millis();
    const bool ok = ::truncate(path, (off_t)len) == 0;
    noteStall(t0);
    return ok;
}

void list(Print& out) {
    if (!mounted) { out.println("sdlog: no card"); return; }
    File root = SD.open("/");
    if (!root) { out.println("sdlog: cannot open root"); return; }
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        out.printf("%8lu  %s\n", (unsigned long)f.size(), f.name());
        f.close();
    }
    root.close();
}

bool dump(Print& out, const char* name) {
    if (!mounted || !name || !name[0]) return false;
    File f = SD.open(name, FILE_READ);
    if (!f) return false;
    uint8_t buf[256];
    while (f.available()) {
        const int k = f.read(buf, sizeof buf);
        if (k <= 0) break;
        out.write(buf, (size_t)k);
    }
    f.close();
    return true;
}

uint32_t maxIndex(const char* prefix, const char* suffix) {
    if (!mounted || !prefix || !suffix) return 0;
    // f.name() returns the bare basename (no leading '/'), so compare the
    // prefix without its leading '/' too.
    if (prefix[0] == '/') prefix++;
    const size_t plen = strlen(prefix);
    File root = SD.open("/");
    if (!root) return 0;
    uint32_t hi = 0;
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        const char* nm = f.name();
        if (nm[0] == '/') nm++;
        if (!strncmp(nm, prefix, plen)) {
            const char* p = nm + plen;
            uint32_t v = 0;
            bool any = false;
            while (*p >= '0' && *p <= '9') { v = v * 10 + (uint32_t)(*p - '0'); p++; any = true; }
            if (any && !strcmp(p, suffix) && v > hi) hi = v;
        }
        f.close();
    }
    root.close();
    return hi;
}

uint32_t sizeOf(const char* name) {
    if (!mounted || !name || !name[0]) return 0;
    File f = SD.open(name, FILE_READ);
    if (!f) return 0;
    const uint32_t n = (uint32_t)f.size();
    f.close();
    return n;
}

bool append(const char* name, const uint8_t* data, uint32_t len) {
    if (!mounted || !name || !name[0] || !data) return false;
    const uint32_t t0 = millis();
    File f = SD.open(name, FILE_APPEND);
    if (!f) { noteStall(t0); return false; }
    const size_t w = f.write(data, len);
    f.close();
    noteStall(t0);
    return w == len;
}

uint32_t takeMaxStallMs() {
    const uint32_t v = stallMax;
    stallMax = 0;
    return v;
}

bool rotate(const char* name, const char* to) {
    if (!mounted || !name || !to) return false;
    if (!SD.exists(name)) return false;     // nothing to move yet
    SD.remove(to);                          // clear a stale previous generation
    return SD.rename(name, to);
}

} // namespace sdlog
