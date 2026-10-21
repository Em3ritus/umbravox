// Theme packs off the SD card. See themeload.h.
//
// Everything a pack decides goes through ui/themepack (host-tested): this file
// lists, measures and reads files, keeps the boot-loop guard in NVS, and
// hands parsed packs to pal::addPack. Every buffer is fixed: the folder list,
// the theme.json buffer, and at most MAX_PACKS emblem buffers of MAX_EMBLEM
// bytes each in PSRAM. Nothing is sized from a file.
#include "themeload.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <stdio.h>
#include "sdlog.h"
#include "elog.h"
#include "palette.h"
#include "themepack.h"

namespace themeload {
namespace {

using namespace themepack;

constexpr const char* DIR = "/themes";
constexpr const char* NS  = "aether";        // device/prefs.cpp's namespace
constexpr const char* KEY = "loading";
// Folders considered per boot: the MAX_FOLDERS smallest names (keepName), so
// the choice does not depend on directory order. 32 is four times the packs
// that can load; anything past it is counted, logged and skipped.
constexpr int    MAX_FOLDERS = 32;
constexpr size_t REC_CAP = 48;               // formatLoading: 31 + ':' + 10 + NUL

uint8_t  nSkipped = 0;

char     names[MAX_FOLDERS][32];
uint32_t jsonSz[MAX_FOLDERS], embSz[MAX_FOLDERS];
int      nNames = 0;
uint16_t nSeen = 0;                          // valid-named folders listed

uint8_t  json[MAX_JSON];                     // one theme.json at a time

// The registered packs' emblems. pal::addPack stores the Emblem POINTER and
// Emblem::px points into its buffer, so both live for the whole program:
// the structs here, the pixels in PSRAM, never freed. `spare` is a buffer
// allocated for a pack that then failed to load; the next emblem pack reuses
// it, so at most MAX_PACKS buffers are ever allocated.
Emblem      emblems[MAX_PACKS];
uint8_t*    spare = nullptr;

const char* taken[MAX_PACKS];                // admitted names, for admit()
int         nTaken = 0;

// The boot-loop guard. `record` is what NVS held at boot; `keep` says it
// still names a folder at its recorded size, so it must survive this boot;
// `cur` is what NVS holds now, so nothing is written that is already there.
Preferences nvs;
char        record[REC_CAP] = {};
char        cur[REC_CAP] = {};
bool        keep = false;

void countSkip() { if (nSkipped < 255) nSkipped++; }

// A folder name off the card is hostile until folderNameOk says otherwise:
// for the log, keep it printable and short.
void logSafe(char* out, size_t cap, const char* in) {
    size_t o = 0;
    for (; in && in[o] && o < 31 && o + 1 < cap; o++)
        out[o] = (in[o] >= 0x20 && in[o] < 0x7F) ? in[o] : '?';
    out[o] = 0;
}

void skip(const char* folder, const char* why) {
    char safe[32];
    logSafe(safe, sizeof safe, folder);
    elog::event(elog::WARN, "theme", "%s: %s", safe, why);
    countSkip();
}

// False if the path would not fit (it always does for a folderNameOk name:
// 8 + 31 + 1 + 10 < 64), in which case `out` is empty and every read fails.
bool pathOf(char* out, size_t cap, const char* folder, const char* file) {
    const int w = snprintf(out, cap, "%s/%s/%s", DIR, folder, file);
    if (w > 0 && (size_t)w < cap) return true;
    out[0] = 0;
    return false;
}

// --- listing ---------------------------------------------------------------

void onEntry(const char* name, bool isDir, void*) {
    if (!isDir) return;                      // a file in /themes is not a pack
    if (!folderNameOk(name)) { skip(name, admitWhy(ADMIT_BAD_FOLDER)); return; }
    if (nSeen < 0xFFFF) nSeen++;
    nNames = keepName(names, nNames, MAX_FOLDERS, name);
}

void measure() {
    char path[64];
    for (int i = 0; i < nNames; i++) {
        jsonSz[i] = pathOf(path, sizeof path, names[i], "theme.json") ? sdlog::sizeOf(path) : 0;
        embSz[i]  = pathOf(path, sizeof path, names[i], "emblem.bin") ? sdlog::sizeOf(path) : 0;
    }
}

uint32_t totalOf(int i) {                    // saturating: a 4 GB file cannot wrap it
    const uint64_t t = (uint64_t)jsonSz[i] + embSz[i];
    return t > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)t;
}

// --- boot-loop guard -------------------------------------------------------

bool guardPut(const char* v) {
    if (!strcmp(cur, v)) return true;
    const bool ok = v[0] ? nvs.putString(KEY, v) == strlen(v) : nvs.remove(KEY);
    if (ok) snprintf(cur, sizeof cur, "%s", v);
    return ok;
}

// Read the record, decide whether it still applies, clear it if not.
void guardBegin() {
    const bool had = nvs.isKey(KEY);
    if (had) nvs.getString(KEY, record, sizeof record);   // too long reads as ""
    record[sizeof record - 1] = 0;
    for (int i = 0; i < nNames && !keep; i++)
        keep = skipAfterCrash(record, names[i], totalOf(i));
    if (keep) {
        snprintf(cur, sizeof cur, "%s", record);
    } else {
        // The folder is gone, or changed size (edited: it gets another go),
        // or the record is junk. One erase, only if there is something.
        record[0] = 0;
        cur[0] = 0;
        if (had) nvs.remove(KEY);
    }
}

// After a folder: back to the kept record, or to nothing.
void guardEnd() { guardPut(keep ? record : ""); }

// --- one pack --------------------------------------------------------------

const Emblem* loadEmblem(const char* folder, uint32_t size, char* why, size_t whyCap) {
    if (size == 0) { snprintf(why, whyCap, "centre is emblem but emblem.bin is missing"); return nullptr; }
    if (size > MAX_EMBLEM) { snprintf(why, whyCap, "emblem.bin over %u bytes", (unsigned)MAX_EMBLEM); return nullptr; }
    if (!spare) spare = (uint8_t*)heap_caps_malloc(MAX_EMBLEM, MALLOC_CAP_SPIRAM);
    if (!spare) { snprintf(why, whyCap, "no PSRAM for emblem.bin"); return nullptr; }
    char path[64];
    const uint32_t n = pathOf(path, sizeof path, folder, "emblem.bin")
                     ? sdlog::readFile(path, spare, (uint32_t)MAX_EMBLEM) : 0;
    if (n != size) { snprintf(why, whyCap, "emblem.bin could not be read"); return nullptr; }
    Emblem& e = emblems[nTaken];             // the slot the next admitted pack takes
    char ewhy[40];
    if (!parseEmblem(spare, n, e, ewhy, sizeof ewhy)) {
        snprintf(why, whyCap, "emblem.bin: %s", ewhy);
        return nullptr;
    }
    return &e;
}

bool loadPack(const char* folder, uint32_t js, uint32_t es, char* why, size_t whyCap) {
    char path[64];
    const uint32_t n = pathOf(path, sizeof path, folder, "theme.json")
                     ? sdlog::readFile(path, json, (uint32_t)sizeof json) : 0;
    if (n == 0 || n != js) { snprintf(why, whyCap, "theme.json could not be read"); return false; }

    Pack p;
    char pwhy[48];
    if (!parse((const char*)json, n, p, pwhy, sizeof pwhy)) {
        snprintf(why, whyCap, "theme.json: %s", pwhy);
        return false;
    }
    const Emblem* em = nullptr;
    if (p.centre == CENTRE_EMBLEM && !(em = loadEmblem(folder, es, why, whyCap))) return false;

    const Admit a = admit(folder, p, taken, nTaken);
    if (a != ADMIT_OK) { snprintf(why, whyCap, "%s", admitWhy(a)); return false; }

    Pack probe = p;                          // addPack sanitises its own copy
    if (sanitizeStatus(probe))
        elog::event(elog::WARN, "theme", "%s: status colours too faint, replaced", folder);
    const int id = pal::addPack(p, em);
    if (id < 0) { snprintf(why, whyCap, "%s", admitWhy(ADMIT_FULL)); return false; }
    taken[nTaken++] = pal::themeName((uint8_t)id);   // static storage in palette.cpp
    if (em) spare = nullptr;                 // that buffer now belongs to the pack
    elog::event(elog::INFO, "theme", "%s: loaded \"%s\" as theme %d", folder, p.name, id);
    return true;
}

void visit(int i) {
    const char* folder = names[i];
    const uint32_t total = totalOf(i);
    if (skipAfterCrash(record, folder, total)) {
        skip(folder, "skipped, a reset happened while loading it");
        return;
    }
    if (nTaken >= MAX_PACKS) { skip(folder, admitWhy(ADMIT_FULL)); return; }
    if (jsonSz[i] == 0) { skip(folder, "no theme.json"); return; }
    if (jsonSz[i] > MAX_JSON) { skip(folder, "theme.json over 4096 bytes"); return; }

    char rec[REC_CAP];
    formatLoading(rec, sizeof rec, folder, total);
    if (!guardPut(rec)) { skip(folder, "could not set the boot-loop guard in NVS"); return; }
    char why[96] = "unknown";
    const bool ok = loadPack(folder, jsonSz[i], embSz[i], why, sizeof why);
    guardEnd();
    if (!ok) skip(folder, why);
}

} // namespace

void scan() {
    if (!sdlog::present()) return;           // no card: nothing loaded, nothing skipped
    nNames = 0; nSeen = 0;
    if (!sdlog::listDir(DIR, onEntry, nullptr)) return;   // no /themes: normal
    if (nNames == 0) return;
    if (nSeen > nNames) {
        elog::event(elog::WARN, "theme", "%u more pack folders not read (limit %d)",
                    (unsigned)(nSeen - nNames), MAX_FOLDERS);
        for (int i = nNames; i < nSeen; i++) countSkip();
    }
    sortNames(names, nNames);
    measure();
    // Without the guard a pack that crashes the parser would trap the board in
    // a reset loop, so no NVS means no packs.
    if (!nvs.begin(NS, /*readOnly=*/false)) {
        elog::event(elog::ERR, "theme", "NVS unavailable, %d pack folders not loaded", nNames);
        for (int i = 0; i < nNames; i++) countSkip();
        return;
    }
    guardBegin();
    for (int i = 0; i < nNames; i++) visit(i);
    nvs.end();
    elog::event(elog::INFO, "theme", "%d packs loaded, %u skipped", nTaken, (unsigned)nSkipped);
}

uint8_t skipped() { return nSkipped; }

} // namespace themeload
