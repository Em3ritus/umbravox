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
// anywhere on disk ("/packs/ember") but its folder is "ember".
// Header-only and static: include it from one translation unit per binary.
#pragma once
#include "../ui/palette.h"
#include "../ui/themepack.h"
#include "../ui/themelayout.h"
#include "../ui/packassets.h"
#include <vector>
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

// Clip bytes live as long as the program too (pal::addPack keeps the pointer): at most MAX_CLIPS, as on the board.
inline uint8_t* clipBuf(int i) { static uint8_t b[themepack::MAX_CLIPS][themepack::MAX_CLIP]; return b[i]; }
inline themepack::Clip* clipSlot(int i) { static themepack::Clip c[themepack::MAX_CLIPS]; return &c[i]; }
inline int& clipsUsed() { static int n = 0; return n; }

// A pack's resolved layout, by pal pack slot. Lives as long as the program (pal keeps the pointer).
inline themelayout::PackLayout* layoutSlot(int s) { static themelayout::PackLayout l[themepack::MAX_PACKS]; return &l[s]; }

// A pack's fonts and pictures (only the ones its layout refers to), by pal pack slot. The device reads these when
// the theme is selected; the host has no selection-time loader, so it reads them with the pack and useMedia()
// hands them to the registry, as the device's loader does when it finishes.
struct Media {
    std::vector<uint8_t> fontBytes[assets::MAX_FONTS], artBytes[assets::MAX_ARTS];
    fontpack::Font font[assets::MAX_FONTS];
    artpack::Art art[assets::MAX_ARTS];
    bool fontOk[assets::MAX_FONTS] = {}, artOk[assets::MAX_ARTS] = {};
};
inline Media* mediaSlot(int s) { static Media m[themepack::MAX_PACKS]; return &m[s]; }

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

// Reads every font and picture the pack's looks refer to. False (layout to be skipped) when one is missing or over
// its cap, or one board's pictures add up to more than MAX_ART_TOTAL; a file that does not parse is only absent.
inline bool loadMedia(const char* dir, const themelayout::PackLayout& L, Media& m, char* why, size_t whyCap) {
    m = Media();
    uint8_t fonts = 0; uint16_t arts = 0;
    for (int b = 0; b < themelayout::B_N; b++) {
        fonts |= L.look[b].fontsUsed; arts |= L.look[b].artsUsed;
        unsigned long long total = 0;
        for (int i = 0; i < assets::MAX_ARTS; i++) {
            if (!(L.look[b].artsUsed & (1u << i))) continue;
            char path[460]; snprintf(path, sizeof path, "%s/a%d.art", dir, i);
            const long long n = fileSize(path);
            if (n > 0) total += (unsigned long long)n;
        }
        if (total > artpack::MAX_ART_TOTAL) {
            if (whyCap) snprintf(why, whyCap, "layout.json: the %s board's pictures add up to over %u bytes", b ? "cyd" : "waveshare", (unsigned)artpack::MAX_ART_TOTAL);
            return false;
        }
    }
    for (int k = 0; k < assets::MAX_FONTS + assets::MAX_ARTS; k++) {
        const bool isFont = k < assets::MAX_FONTS;
        const int i = isFont ? k : k - assets::MAX_FONTS;
        if (isFont ? !(fonts & (1u << i)) : !(arts & (1u << i))) continue;
        char path[460], name[12];
        snprintf(name, sizeof name, isFont ? "f%d.fnt" : "a%d.art", i);
        snprintf(path, sizeof path, "%s/%s", dir, name);
        const long long n = fileSize(path);
        const size_t cap = isFont ? fontpack::MAX_FONT : artpack::MAX_ART;
        if (n <= 0) { if (whyCap) snprintf(why, whyCap, "layout.json: %s is missing", name); return false; }
        if ((unsigned long long)n > cap) { if (whyCap) snprintf(why, whyCap, "layout.json: %s over %u bytes", name, (unsigned)cap); return false; }
        std::vector<uint8_t>& bytes = isFont ? m.fontBytes[i] : m.artBytes[i];
        bytes.resize((size_t)n);
        if (!readAll(path, bytes.data(), bytes.size())) { if (whyCap) snprintf(why, whyCap, "layout.json: %s could not be read", name); return false; }
        const char* pw = "";
        if (isFont) m.fontOk[i] = fontpack::parse(bytes.data(), bytes.size(), m.font[i], &pw);
        else m.artOk[i] = artpack::parse(bytes.data(), bytes.size(), m.art[i], &pw);
    }
    return true;
}

} // namespace detail

// Hands pack `id`'s fonts and pictures to the registry (what the device's loader does once they are read).
inline bool useMedia(int id) {
    const int s = pal::packSlot((uint8_t)id);
    if (s < 0) return false;
    const detail::Media& m = *detail::mediaSlot(s);
    const fontpack::Font* fonts[assets::MAX_FONTS] = {};
    const artpack::Art* arts[assets::MAX_ARTS] = {};
    for (int i = 0; i < assets::MAX_FONTS; i++) if (m.fontOk[i]) fonts[i] = &m.font[i];
    for (int i = 0; i < assets::MAX_ARTS; i++) if (m.artOk[i]) arts[i] = &m.art[i];
    assets::set((uint8_t)id, fonts, arts);
    return true;
}

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

    char jsonPath[460], emblemPath[460], clipPath[460], layoutPath[460];
    snprintf(jsonPath, sizeof jsonPath, "%s/theme.json", dir);
    snprintf(layoutPath, sizeof layoutPath, "%s/layout.json", dir);
    snprintf(emblemPath, sizeof emblemPath, "%s/emblem.bin", dir);
    snprintf(clipPath, sizeof clipPath, "%s/radar.vid", dir);

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

    // radar.vid is optional; a bad one skips the whole pack, as a bad emblem does.
    const Clip* clip = nullptr;
    const long long cs = detail::fileSize(clipPath);
    if (cs >= 0) {
        if ((unsigned long long)cs > MAX_CLIP) { if (whyCap) snprintf(why, whyCap, "radar.vid over %u bytes", (unsigned)MAX_CLIP); return ERR; }
        if (detail::clipsUsed() >= MAX_CLIPS) { if (whyCap) snprintf(why, whyCap, "no room for another clip (%d at most)", MAX_CLIPS); return ERR; }
        uint8_t* cb = detail::clipBuf(detail::clipsUsed());
        if (!detail::readAll(clipPath, cb, (size_t)cs)) { detail::fail(why, whyCap, "radar.vid could not be read"); return ERR; }
        Clip& c = *detail::clipSlot(detail::clipsUsed());
        char cwhy[48];
        if (!parseClip(cb, (size_t)cs, c, cwhy, sizeof cwhy)) { detail::fail(why, whyCap, "%s", cwhy); return ERR; }
        clip = &c;
    }

    // Names already registered (static storage in palette.cpp).
    const char* taken[MAX_PACKS];
    int nTaken = 0;
    for (int id = pal::TH_COUNT; id < pal::themeCount() && nTaken < MAX_PACKS; id++)
        taken[nTaken++] = pal::themeName((uint8_t)id);
    const Admit a = admit(folder, p, taken, nTaken);
    if (a != ADMIT_OK) { detail::fail(why, whyCap, "%s", admitWhy(a)); return ERR; }

    const int id = pal::addPack(p, em, clip);
    if (id < 0) { detail::fail(why, whyCap, "%s", admitWhy(ADMIT_FULL)); return ERR; }
    if (em) detail::emblemsUsed()++;      // that buffer now belongs to the pack
    if (clip) detail::clipsUsed()++;

    // layout.json is optional; a bad one skips only the layout (the reason goes in why, the pack stays).
    const long long ls = detail::fileSize(layoutPath);
    if (ls >= 0) {
        const int s = pal::packSlot((uint8_t)id);
        static uint8_t lbuf[themelayout::MAX_JSON];
        if (s < 0) {
            detail::fail(why, whyCap, "layout.json: no pack slot");
        } else if ((unsigned long long)ls > themelayout::MAX_JSON) {
            if (whyCap) snprintf(why, whyCap, "layout.json over %u bytes", (unsigned)themelayout::MAX_JSON);
        } else if (!detail::readAll(layoutPath, lbuf, (size_t)ls)) {
            detail::fail(why, whyCap, "layout.json could not be read");
        } else if (themelayout::build((const char*)lbuf, (size_t)ls, *detail::layoutSlot(s), why, whyCap, p.color) &&
                   detail::loadMedia(dir, *detail::layoutSlot(s), *detail::mediaSlot(s), why, whyCap)) {
            pal::setPackLayout(id, &detail::layoutSlot(s)->lo);
        }
    }
    return id;
}

} // namespace packload
