// Theme packs off the SD card. See themeload.h.
//
// Everything a pack decides goes through ui/themepack (host-tested): this file
// lists, measures and reads files, backs the boot-loop guard's GuardKv with
// Preferences, and hands parsed packs to pal::addPack. Every buffer is fixed:
// the folder list, the theme.json buffer, and at most MAX_PACKS emblem buffers
// of MAX_EMBLEM bytes each in PSRAM, and at most MAX_CLIPS radar clips, each allocated at its measured size (already
// checked against MAX_CLIP) in PSRAM. Nothing else is sized from a file.
//
// Buffer capacities are never passed by hand: every helper that fills a
// buffer takes it as an array reference (char (&)[N]) and derives N itself,
// so a pointer cannot be handed in where an array was meant; the rest use
// capOf(), which does not compile on a pointer. No sizeof on a buffer here.
#include "themeload.h"
#include <new>
#include <type_traits>
#include <utility>
#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "sdlog.h"
#include "elog.h"
#include "palette.h"
#include "themepack.h"

namespace themeload {
namespace {

using namespace themepack;

constexpr const char* DIR = "/themes";
constexpr const char* NS  = "aether";        // device/prefs.cpp's namespace
// Folders considered per boot: the MAX_FOLDERS smallest names (keepName), so
// the choice does not depend on directory order. 32 is four times the packs
// that can load; anything past it is counted, logged and skipped.
constexpr int    MAX_FOLDERS = BAD_MAX;     // the crash list needs one slot per folder
constexpr size_t PATH_CAP = 64;              // "/themes/" + 31 + "/" + "theme.json" + NUL = 51
constexpr size_t WHY_CAP  = 96;

uint8_t  nSkipped = 0;

int      nNames = 0;
uint16_t nSeen = 0;                          // valid-named folders listed

// Everything only scan() needs: the folder names and sizes, one theme.json
// at a time (the guard's scratch is needed only before the first theme.json
// is read, so the two share storage), and the crash list. About 6.6 KB, so it
// lives in PSRAM for the length of scan() and is freed after: internal RAM is
// what Wi-Fi and BLE run out of.
struct Scratch {
    char     names[MAX_FOLDERS][32];
    uint32_t jsonSz[MAX_FOLDERS], embSz[MAX_FOLDERS], clipSz[MAX_FOLDERS], sndSz[MAX_FOLDERS];
    union {
        uint8_t      json[MAX_JSON];
        GuardScratch guard;
    } buf;
    BadList  bad;
};
Scratch* S = nullptr;

// The registered packs' emblems. pal::addPack stores the Emblem POINTER and
// Emblem::px points into its buffer, so both live for the whole program:
// the structs here, the pixels in PSRAM, never freed. `spare` is a buffer
// allocated for a pack that then failed to load; the next emblem pack reuses
// it, so at most MAX_PACKS buffers are ever allocated.
Emblem      emblems[MAX_PACKS];
uint8_t*    spare = nullptr;

// Radar clips (radar.vid). pal::addPack keeps the Clip POINTER and Clip::px points into the PSRAM buffer, so both live
// for the whole program. At most MAX_CLIPS: 2 x 780 KB is what PSRAM can spare.
Clip        clips[MAX_CLIPS];
int         nClips = 0;
// Alert sounds (alert.snd), kept the same way: pal::addPack keeps the Sound POINTER and its samples live in a PSRAM
// buffer for the whole program. At most MAX_SOUNDS sets.
Sound       sounds[MAX_SOUNDS];
int         nSounds = 0;

const char* taken[MAX_PACKS];                // admitted names, for admit()
int         nTaken = 0;

// The boot-loop guard. The decisions (themepack::guardBoot, guardSet,
// guardClear, badMatch) are shared with the host test; this is the NVS
// behind them. `bad` is the list of packs that have crashed the loader.
Preferences nvs;
GuardCur    cur;

bool   kvIsKey(void*, const char* k)                              { return nvs.isKey(k); }
size_t kvGet(void*, const char* k, char* b, size_t maxLen)        { return nvs.getString(k, b, maxLen); }
size_t kvPut(void*, const char* k, const char* v)                 { return nvs.putString(k, v); }
bool   kvRemove(void*, const char* k)                             { return nvs.remove(k); }
const GuardKv KV = {nullptr, kvIsKey, kvGet, kvPut, kvRemove};

void countSkip() { if (nSkipped < 255) nSkipped++; }

// A folder name off the card is hostile until folderNameOk says otherwise:
// for the log, keep it printable and at most 31 characters.
template <size_t N>
void logSafe(char (&out)[N], const char* in) {
    static_assert(N >= 32, "room for a 31-character name");
    size_t o = 0;
    for (; in && in[o] && o < 31; o++)
        out[o] = (in[o] >= 0x20 && in[o] < 0x7F) ? in[o] : '?';
    out[o] = 0;
}

void skip(const char* folder, const char* why) {
    char safe[32];
    logSafe(safe, folder);
    elog::event(elog::WARN, "theme", "%s: %s", safe, why);
    countSkip();
}

template <size_t N>
__attribute__((format(printf, 2, 3)))
void setWhy(char (&why)[N], const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, N, fmt, ap);
    va_end(ap);
}

// False if the path would not fit (it always does for a folderNameOk name),
// in which case `out` is empty and every read fails.
bool pathOf(char (&out)[PATH_CAP], const char* folder, const char* file) {
    const int w = snprintf(out, PATH_CAP, "%s/%s/%s", DIR, folder, file);
    if (w > 0 && (size_t)w < PATH_CAP) return true;
    out[0] = 0;
    return false;
}

// --- listing ---------------------------------------------------------------

void onEntry(const char* name, bool isDir, void*) {
    if (!isDir) return;                      // a file in /themes is not a pack
    if (!folderNameOk(name)) { skip(name, admitWhy(ADMIT_BAD_FOLDER)); return; }
    if (nSeen < 0xFFFF) nSeen++;
    nNames = keepName(S->names, nNames, MAX_FOLDERS, name);
}

void measure() {
    char path[PATH_CAP];
    for (int i = 0; i < nNames; i++) {
        S->jsonSz[i] = pathOf(path, S->names[i], "theme.json") ? sdlog::sizeOf(path) : 0;
        S->embSz[i]  = pathOf(path, S->names[i], "emblem.bin") ? sdlog::sizeOf(path) : 0;
        S->clipSz[i] = pathOf(path, S->names[i], "radar.vid")  ? sdlog::sizeOf(path) : 0;
        S->sndSz[i]  = pathOf(path, S->names[i], "alert.snd")  ? sdlog::sizeOf(path) : 0;
    }
}

uint32_t totalOf(int i) {                    // saturating: a 4 GB file cannot wrap it
    const uint64_t t = (uint64_t)S->jsonSz[i] + S->embSz[i] + S->clipSz[i] + S->sndSz[i];   // an edited clip gives the pack another try
    return t > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)t;
}

// --- boot-loop guard -------------------------------------------------------

// Fold a crash left by the last boot into the list (guardBoot) and log what
// it found. False: no pack may be read this boot.
bool guardBegin() {
    uint32_t sizes[MAX_FOLDERS];
    for (int i = 0; i < nNames; i++) sizes[i] = totalOf(i);
    const GuardBootResult g = guardBoot(KV, S->names, sizes, nNames, S->bad, S->buf.guard, cur);
    if (g.listJunk)
        elog::event(elog::WARN, "theme", "crash list in NVS was unreadable; replaced");
    if (g.folded[0]) {
        char safe[32];
        logSafe(safe, g.folded);
        elog::event(elog::WARN, "theme", "reset while loading %s; skipped until it changes", safe);
    }
    if (g.listWriteFailed)
        elog::event(elog::ERR, "theme", "could not save the crash list in NVS");
    if (g.loadingClearFailed)
        elog::event(elog::ERR, "theme", "could not clear the loading record in NVS");
    return g.ok;
}

// --- one pack --------------------------------------------------------------

const Emblem* loadEmblem(const char* folder, uint32_t size, char (&why)[WHY_CAP]) {
    if (size == 0) { setWhy(why, "centre is emblem but emblem.bin is missing"); return nullptr; }
    if (size > MAX_EMBLEM) { setWhy(why, "emblem.bin over %u bytes", (unsigned)MAX_EMBLEM); return nullptr; }
    if (!spare) spare = (uint8_t*)heap_caps_malloc(MAX_EMBLEM, MALLOC_CAP_SPIRAM);
    if (!spare) { setWhy(why, "no PSRAM for emblem.bin"); return nullptr; }
    char path[PATH_CAP];
    const uint32_t n = pathOf(path, folder, "emblem.bin")
                     ? sdlog::readFile(path, spare, (uint32_t)MAX_EMBLEM) : 0;
    if (n != size) { setWhy(why, "emblem.bin could not be read"); return nullptr; }
    Emblem& e = emblems[nTaken];             // the slot the next admitted pack takes
    char ewhy[40];
    if (!parseEmblem(spare, n, e, ewhy, capOf(ewhy))) {
        setWhy(why, "emblem.bin: %s", ewhy);
        return nullptr;
    }
    return &e;
}

// radar.vid: read whole into its own PSRAM buffer, then validated by themepack::parseClip. Null with `why` set on any
// failure (the buffer is freed); on success the buffer and its slot belong to the pack, to be released by
// releaseClip() if the pack is then refused.
const Clip* loadClip(const char* folder, uint32_t size, char (&why)[WHY_CAP], uint8_t*& buf) {
    buf = nullptr;
    if (nClips >= MAX_CLIPS) { setWhy(why, "no room for another clip (%d at most)", MAX_CLIPS); return nullptr; }
    if (size > MAX_CLIP) { setWhy(why, "radar.vid over %u bytes", (unsigned)MAX_CLIP); return nullptr; }
    buf = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!buf) { setWhy(why, "no PSRAM for radar.vid"); return nullptr; }
    char path[PATH_CAP];
    const uint32_t n = pathOf(path, folder, "radar.vid") ? sdlog::readFile(path, buf, size) : 0;
    if (n != size) { setWhy(why, "radar.vid could not be read"); heap_caps_free(buf); buf = nullptr; return nullptr; }
    Clip& c = clips[nClips];                 // the slot the next admitted clip takes
    char cwhy[48];
    if (!parseClip(buf, n, c, cwhy, capOf(cwhy))) {
        setWhy(why, "%s", cwhy);             // already names radar.vid
        heap_caps_free(buf); buf = nullptr;
        return nullptr;
    }
    return &c;
}

// alert.snd: the same, into its own PSRAM buffer, validated by themepack::parseSound.
const Sound* loadSound(const char* folder, uint32_t size, char (&why)[WHY_CAP], uint8_t*& buf) {
    buf = nullptr;
    if (nSounds >= MAX_SOUNDS) { setWhy(why, "no room for more alert sounds (%d sets at most)", MAX_SOUNDS); return nullptr; }
    if (size > MAX_SOUND) { setWhy(why, "alert.snd over %u bytes", (unsigned)MAX_SOUND); return nullptr; }
    buf = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!buf) { setWhy(why, "no PSRAM for alert.snd"); return nullptr; }
    char path[PATH_CAP];
    const uint32_t n = pathOf(path, folder, "alert.snd") ? sdlog::readFile(path, buf, size) : 0;
    if (n != size) { setWhy(why, "alert.snd could not be read"); heap_caps_free(buf); buf = nullptr; return nullptr; }
    Sound& s = sounds[nSounds];
    char swhy[48];
    if (!parseSound(buf, n, s, swhy, capOf(swhy))) { setWhy(why, "%s", swhy); heap_caps_free(buf); buf = nullptr; return nullptr; }
    return &s;
}

bool loadPack(const char* folder, uint32_t js, uint32_t es, uint32_t cs, uint32_t ss, char (&why)[WHY_CAP]) {
    static_assert(std::extent<std::remove_reference_t<decltype(std::declval<Scratch&>().buf.json)>>::value
                  == MAX_JSON, "theme.json buffer");
    char path[PATH_CAP];
    const uint32_t n = pathOf(path, folder, "theme.json")
                     ? sdlog::readFile(path, S->buf.json, (uint32_t)MAX_JSON) : 0;
    if (n == 0 || n != js) { setWhy(why, "theme.json could not be read"); return false; }

    Pack p;
    char pwhy[48];
    if (!parse((const char*)S->buf.json, n, p, pwhy, capOf(pwhy))) {
        setWhy(why, "theme.json: %s", pwhy);
        return false;
    }
    const Emblem* em = nullptr;
    if ((p.centre == CENTRE_EMBLEM || p.centre == CENTRE_RADAR_EMBLEM) && !(em = loadEmblem(folder, es, why))) return false;

    uint8_t* clipBuf = nullptr;
    const Clip* clip = nullptr;
    if (cs > 0 && !(clip = loadClip(folder, cs, why, clipBuf))) return false;
    uint8_t* sndBuf = nullptr;
    const Sound* snd = nullptr;
    if (ss > 0 && !(snd = loadSound(folder, ss, why, sndBuf))) { if (clipBuf) heap_caps_free(clipBuf); return false; }

    const Admit a = admit(folder, p, taken, nTaken);
    if (a != ADMIT_OK) { setWhy(why, "%s", admitWhy(a)); if (clipBuf) heap_caps_free(clipBuf); if (sndBuf) heap_caps_free(sndBuf); return false; }

    Pack probe = p;                          // addPack sanitises its own copy
    if (sanitizeStatus(probe))
        elog::event(elog::WARN, "theme", "%s: status colours too faint, replaced", folder);
    const int id = pal::addPack(p, em, clip, snd);
    if (id < 0) { setWhy(why, "%s", admitWhy(ADMIT_FULL)); if (clipBuf) heap_caps_free(clipBuf); if (sndBuf) heap_caps_free(sndBuf); return false; }
    if (snd) nSounds++;
    if (clip) nClips++;                      // that buffer and slot now belong to the pack
    taken[nTaken++] = pal::themeName((uint8_t)id);   // static storage in palette.cpp
    if (em) spare = nullptr;                 // that buffer now belongs to the pack
    elog::event(elog::INFO, "theme", "%s: loaded \"%s\" as theme %d%s%s", folder, p.name, id, clip ? " with a radar clip" : "", snd ? " with alert sounds" : "");
    return true;
}

void visit(int i) {
    const char* folder = S->names[i];
    const uint32_t total = totalOf(i);
    if (badMatch(S->bad, folder, total)) {
        skip(folder, "skipped, a reset happened while loading it");
        return;
    }
    if (nTaken >= MAX_PACKS) { skip(folder, admitWhy(ADMIT_FULL)); return; }
    if (S->jsonSz[i] == 0) { skip(folder, "no theme.json"); return; }
    if (S->jsonSz[i] > MAX_JSON) { skip(folder, "theme.json over 4096 bytes"); return; }
    if (S->clipSz[i] > MAX_CLIP) { skip(folder, "radar.vid over 780312 bytes"); return; }
    if (S->sndSz[i] > MAX_SOUND) { skip(folder, "alert.snd over 576020 bytes"); return; }

    char rec[BAD_REC];
    formatLoading(rec, capOf(rec), folder, total);
    if (!guardSet(KV, cur, rec)) { skip(folder, "could not set the boot-loop guard in NVS"); return; }
    char why[WHY_CAP] = "unknown";
    const bool ok = loadPack(folder, S->jsonSz[i], S->embSz[i], S->clipSz[i], S->sndSz[i], why);
    guardClear(KV, cur);                     // loaded or cleanly skipped: nothing in flight
    if (!ok) skip(folder, why);
}

} // namespace

void scanWith();

void scan() {
    if (!sdlog::present()) return;           // no card: nothing loaded, nothing skipped
    void* p = heap_caps_malloc(sizeof(Scratch), MALLOC_CAP_SPIRAM);
    if (!p) {
        // The unit requires PSRAM; without it, no packs rather than 6.6 KB of
        // internal RAM. Built-in themes still work.
        elog::event(elog::ERR, "theme", "no PSRAM for the pack loader, packs not loaded");
        return;
    }
    S = new (p) Scratch();
    scanWith();
    S->~Scratch();
    heap_caps_free(p);
    S = nullptr;
}

void scanWith() {
    nNames = 0; nSeen = 0;
    if (!sdlog::listDir(DIR, onEntry, nullptr)) return;   // no /themes: normal
    if (nNames == 0) return;
    if (nSeen > nNames) {
        elog::event(elog::WARN, "theme", "%u more pack folders not read (limit %d)",
                    (unsigned)(nSeen - nNames), MAX_FOLDERS);
        for (int i = nNames; i < nSeen; i++) countSkip();
    }
    sortNames(S->names, nNames);
    measure();
    // Without the guard a pack that crashes the parser would trap the board in
    // a reset loop, so no NVS means no packs.
    if (!nvs.begin(NS, /*readOnly=*/false)) {
        elog::event(elog::ERR, "theme", "NVS unavailable, %d pack folders not loaded", nNames);
        for (int i = 0; i < nNames; i++) countSkip();
        return;
    }
    if (!guardBegin()) {
        elog::event(elog::ERR, "theme", "a crash could not be recorded, %d pack folders not loaded", nNames);
        for (int i = 0; i < nNames; i++) countSkip();
        nvs.end();
        return;
    }
    for (int i = 0; i < nNames; i++) visit(i);
    nvs.end();
    elog::event(elog::INFO, "theme", "%d packs loaded, %u skipped", nTaken, (unsigned)nSkipped);
}

uint8_t skipped() { return nSkipped; }

} // namespace themeload
