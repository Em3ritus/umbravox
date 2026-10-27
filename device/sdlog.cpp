#include "sdlog.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "board.h"
#include <unistd.h>
#include "applog.h"

namespace sdlog {
namespace {
// The element count of an ARRAY; does not compile on a pointer (see
// themepack::capOf), so no buffer capacity here can become sizeof(char*).
template <class T, size_t N>
constexpr size_t capOf(const T (&)[N]) { return N; }
bool mounted = false;
uint32_t stallMax = 0;                         // longest write since takeMaxStallMs()
inline void noteStall(uint32_t t0) {
    const uint32_t d = millis() - t0;
    if (d > stallMax) stallMax = d;
}

// SD chip-select, from board.h: CS 10 on the NM-CYD-C5 (its LCD is 23, touch
// 1), CS 9 on the Waveshare C5 3.5 (LCD 8). On both, SCK/MOSI/MISO (6/7/2)
// are the display's bus, and the card joins it.
constexpr int SD_CS = board::PIN_SD_CS;
}

bool begin() {
    // setup() has already brought up the shared SPI bus (SCK 6 / MISO 2 /
    // MOSI 7) on the global SPI instance, and the panel shares it under the
    // HAL lock. SD joins the same bus on its own CS; SD.begin reuses the
    // already-begun SPI and drives CS itself, holding it high between
    // transactions.
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
uint32_t appT0 = 0, appBytes = 0, appSize0 = 0;
char     appName[64] = "";
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
    appSize0 = size;
    snprintf(appName, sizeof appName, "%s", name);
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
    // length under FatFs semantics. But Arduino's VFSFileImpl::flush ignores
    // both results and getWriteError() stays 0, and the last buffered bytes
    // (up to the 4 KB stdio buffer) are only written by that flush -- so a
    // full card would look like success. Check the size on the card instead.
    appF.flush();
    appF.close();
    const uint32_t after = sizeOf(appName);
    const bool ok = applog::closeVerified(appOk, appSize0, appBytes, after);
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
        const int k = f.read(buf, capOf(buf));
        if (k <= 0) break;
        out.write(buf, (size_t)k);
    }
    f.close();
    return true;
}

uint32_t maxIndex(const char* prefix, const char* suffix) {
    if (!mounted || !prefix || !suffix) return 0;
    // f.name() is the bare basename; matchIndex drops a leading '/' on either side.
    File root = SD.open("/");
    if (!root) return 0;
    uint32_t hi = 0;
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        uint32_t v;
        // Case-insensitive (matchIndex): earlier firmware wrote ".CSV".
        if (matchIndex(f.name(), prefix, suffix, v) && v > hi) hi = v;
        f.close();
    }
    root.close();
    return hi;
}

bool listDir(const char* path, void (*cb)(const char* name, bool isDir, void* ctx), void* ctx) {
    if (!mounted || !path || path[0] != '/' || !cb) return false;
    File dir = SD.open(path, FILE_READ);
    if (!dir) return false;
    if (!dir.isDirectory()) { dir.close(); return false; }
    for (uint32_t seen = 0; seen < LIST_MAX; seen++) {
        File f = dir.openNextFile();
        if (!f) break;
        // name() is the bare basename on this core; strip any path anyway.
        const char* nm = f.name();
        if (!nm) nm = "";
        for (const char* p = nm; *p; p++) if (*p == '/') nm = p + 1;
        cb(nm, f.isDirectory(), ctx);
        f.close();
    }
    dir.close();
    return true;
}

uint32_t readFile(const char* path, uint8_t* buf, uint32_t cap) {
    if (!mounted || !path || path[0] != '/' || !buf || cap == 0) return 0;
    File f = SD.open(path, FILE_READ);
    if (!f) return 0;
    if (f.isDirectory()) { f.close(); return 0; }
    uint32_t got = 0;
    while (got < cap) {                 // each pass makes progress or stops
        const size_t k = f.read(buf + got, cap - got);
        if (k == 0 || k > cap - got) break;
        got += (uint32_t)k;
    }
    f.close();
    return got;
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
