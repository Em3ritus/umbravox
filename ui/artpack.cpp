#include "artpack.h"

namespace artpack {

static bool fail(const char** why, const char* r) { if (why) *why = r; return false; }
static uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool parse(const uint8_t* d, size_t n, Art& out, const char** why) {
    out = Art();
    if (!d || n < (size_t)HEADER) return fail(why, "art file too short");
    if (n > MAX_ART) return fail(why, "art file too large");
    if (d[0] != 'U' || d[1] != 'V' || d[2] != 'A' || d[3] != 'R') return fail(why, "not an art file");
    if (d[4] != 1) return fail(why, "unsupported art version");
    const int w = u16(d + 6), h = u16(d + 8), frames = u16(d + 10), ncol = d[12];
    if (w < 1 || w > MAX_ART_W || h < 1 || h > MAX_ART_H) return fail(why, "art size out of range");
    if (frames < 1 || frames > MAX_ART_FRAMES) return fail(why, "art frame count out of range");
    if (ncol < 2 || ncol > 16) return fail(why, "art palette must have 2..16 colours");
    const uint32_t frameBytes = ((uint32_t)w * (uint32_t)h + 1) / 2;
    const size_t need = (size_t)HEADER + (size_t)ncol * 2 + (size_t)frames * 2 + (size_t)frames * frameBytes;
    if (n != need) return fail(why, "art file length does not match its header");
    const uint8_t* p = d + HEADER;
    for (int i = 0; i < ncol; ++i) out.pal[i] = u16(p + i * 2);
    const uint8_t* delays = p + ncol * 2;
    uint32_t cycle = 0;
    for (int i = 0; i < frames; ++i) {
        const uint16_t ms = u16(delays + i * 2);
        if (ms == 0) return fail(why, "art frame delay of 0");
        cycle += ms;
    }
    const uint8_t* px = delays + frames * 2;
    const uint32_t pixels = (uint32_t)w * (uint32_t)h;
    for (int f = 0; f < frames; ++f) {
        const uint8_t* fr = px + (size_t)f * frameBytes;
        for (uint32_t i = 0; i < pixels; ++i) {
            const uint8_t v = (i & 1) ? (fr[i / 2] & 0x0F) : (fr[i / 2] >> 4);
            if (v >= ncol) return fail(why, "art pixel index past its palette");
        }
        if ((pixels & 1) && (fr[pixels / 2] & 0x0F) != 0) return fail(why, "art padding nibble not zero");
    }
    out.w = (uint16_t)w; out.h = (uint16_t)h; out.frames = (uint16_t)frames; out.ncol = (uint8_t)ncol;
    out.transparent = (d[5] & 1) != 0;
    out.delays = delays; out.px = px; out.frameBytes = frameBytes; out.cycleMs = cycle;
    return true;
}

uint16_t frameAt(const Art& a, uint32_t nowMs) {
    if (a.frames <= 1 || a.cycleMs == 0) return 0;
    uint32_t t = nowMs % a.cycleMs;
    for (uint16_t i = 0; i < a.frames; ++i) {
        const uint16_t ms = u16(a.delays + i * 2);
        if (t < ms) return i;
        t -= ms;
    }
    return 0;
}

}  // namespace artpack
