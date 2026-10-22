// Host-side theme pack loader, for the simulator and its tests (stdio, POSIX
// stat). It does what device/themeload.cpp does for one folder, in the same
// order and through the same shared functions, so "does the sim accept this
// pack" and "does the device accept it" are the same question:
//
//   folder name rule -> sizes (before reading) -> theme.json -> parse ->
//   emblem.bin (when the centrepiece is emblem) -> admit -> pal::addPack
//
// What it does not do: the boot-loop guard (NVS) and the 8-pack cap's
// directory listing; admit() still enforces the cap of MAX_PACKS.
//
// The folder rule applies to the LAST path component, so a pack can live
// anywhere on disk ("/home/me/packs/ember") but its folder is "ember".
// Header-only and static: include it from one translation unit per binary.
#pragma once
#include "../ui/palette.h"
#include "../ui/themepack.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

namespace packload {

// A failure is a negative id and a short reason in `why`.
enum { ERR = -1 };

namespace detail {

// Emblem pixels live as long as the program (pal::addPack keeps the pointer).
inline themepack::Emblem* emblemSlot(int i) {
    static themepack::Emblem e[themepack::MAX_PACKS];
    return &e[i];
}
inline uint8_t* emblemBuf(int i) {
    static uint8_t b[themepack::MAX_PACKS][themepack::MAX_EMBLEM];
    return b[i];
}
inline int& emblemsUsed() { static int n = 0; return n; }

inline void fail(char* why, size_t cap, const char* fmt, const char* a = "", unsigned b = 0) {
    if (cap) snprintf(why, cap, fmt, a, b);
}

// Size of a regular file, or -1 (missing, a directory, not stat-able).
inline long long fileSize(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    return (long long)st.st_size;
}

// Read exactly `size` bytes into buf; false on any short read or open error.
inline bool readAll(const char* path, uint8_t* buf, size_t size) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    const size_t n = fread(buf, 1, size, f);
    fclose(f);
    return n == size;
}

} // namespace detail

inline int load(const char* folderPath, char* why, size_t whyCap) {
    using namespace themepack;
    if (whyCap) why[0] = 0;
    if (!folderPath || !folderPath[0]) { detail::fail(why, whyCap, "no folder given"); return ERR; }

    char dir[400];
    if (snprintf(dir, sizeof dir, "%s", folderPath) >= (int)sizeof dir) {
        detail::fail(why, whyCap, "path too long"); return ERR;
    }
    size_t len = strlen(dir);
    while (len > 1 && dir[len - 1] == '/') dir[--len] = 0;
    const char* slash = strrchr(dir, '/');
    const char* folder = slash ? slash + 1 : dir;
    if (!folderNameOk(folder)) { detail::fail(why, whyCap, "%s", admitWhy(ADMIT_BAD_FOLDER)); return ERR; }

    char jsonPath[460], emblemPath[460];
    snprintf(jsonPath, sizeof jsonPath, "%s/theme.json", dir);
    snprintf(emblemPath, sizeof emblemPath, "%s/emblem.bin", dir);

    const long long js = detail::fileSize(jsonPath);
    if (js <= 0) { detail::fail(why, whyCap, js == 0 ? "theme.json is empty" : "no theme.json"); return ERR; }
    if ((unsigned long long)js > MAX_JSON) { detail::fail(why, whyCap, "theme.json over 4096 bytes"); return ERR; }
    static uint8_t json[MAX_JSON];
    if (!detail::readAll(jsonPath, json, (size_t)js)) { detail::fail(why, whyCap, "theme.json could not be read"); return ERR; }

    Pack p;
    char pwhy[48];
    if (!parse((const char*)json, (size_t)js, p, pwhy, sizeof pwhy)) {
        detail::fail(why, whyCap, "theme.json: %s", pwhy); return ERR;
    }

    const Emblem* em = nullptr;
    const int slot = detail::emblemsUsed();
    if (p.centre == CENTRE_EMBLEM) {
        const long long es = detail::fileSize(emblemPath);
        if (es <= 0) { detail::fail(why, whyCap, "centre is emblem but emblem.bin is missing"); return ERR; }
        if ((unsigned long long)es > MAX_EMBLEM) { detail::fail(why, whyCap, "emblem.bin over 9728 bytes"); return ERR; }
        if (slot >= MAX_PACKS) { detail::fail(why, whyCap, "%s", admitWhy(ADMIT_FULL)); return ERR; }
        uint8_t* buf = detail::emblemBuf(slot);
        if (!detail::readAll(emblemPath, buf, (size_t)es)) { detail::fail(why, whyCap, "emblem.bin could not be read"); return ERR; }
        Emblem& e = *detail::emblemSlot(slot);
        char ewhy[40];
        if (!parseEmblem(buf, (size_t)es, e, ewhy, sizeof ewhy)) {
            detail::fail(why, whyCap, "emblem.bin: %s", ewhy); return ERR;
        }
        em = &e;
    }

    // Names already registered (static storage in palette.cpp).
    const char* taken[MAX_PACKS];
    int nTaken = 0;
    for (int id = pal::TH_COUNT; id < pal::themeCount() && nTaken < MAX_PACKS; id++)
        taken[nTaken++] = pal::themeName((uint8_t)id);
    const Admit a = admit(folder, p, taken, nTaken);
    if (a != ADMIT_OK) { detail::fail(why, whyCap, "%s", admitWhy(a)); return ERR; }

    const int id = pal::addPack(p, em);
    if (id < 0) { detail::fail(why, whyCap, "%s", admitWhy(ADMIT_FULL)); return ERR; }
    if (em) detail::emblemsUsed()++;      // that buffer now belongs to the pack
    return id;
}

} // namespace packload
