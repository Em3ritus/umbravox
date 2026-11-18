#include "fontpack.h"

namespace fontpack {

static bool fail(const char** why, const char* r) { if (why) *why = r; return false; }

static const uint8_t* entry(const Font& f, char c) {
    const int i = (int)(unsigned char)c - FIRST;
    if (!f.meta || i < 0 || i >= COUNT) return nullptr;
    return f.meta + i * ENTRY;
}

bool parse(const uint8_t* d, size_t n, Font& out, const char** why) {
    out = Font();
    if (!d || n < (size_t)HEADER) return fail(why, "font file too short");
    if (n > MAX_FONT) return fail(why, "font file too large");
    if (d[0] != 'U' || d[1] != 'V' || d[2] != 'F' || d[3] != 'N') return fail(why, "not a font file");
    if (d[4] != 1) return fail(why, "unsupported font version");
    const int h = d[5];
    if (h < FONT_MIN_H || h > FONT_MAX_H) return fail(why, "font height out of range");
    if (d[6] > h) return fail(why, "font ascent above its height");
    if (d[7] != FIRST || d[8] != COUNT) return fail(why, "font must hold chars 32..126");
    const size_t table = (size_t)HEADER + (size_t)COUNT * ENTRY;
    if (n < table) return fail(why, "font glyph table truncated");
    const uint8_t* meta = d + HEADER;
    const size_t bitsLen = n - table;
    for (int i = 0; i < COUNT; ++i) {
        const uint8_t* e = meta + i * ENTRY;
        const int w = e[1];
        if (w > FONT_MAX_GLYPH_W) return fail(why, "font glyph wider than 32");
        const size_t off = (size_t)e[2] | ((size_t)e[3] << 8);
        const size_t need = (size_t)h * (size_t)((w + 7) / 8);
        if (off > bitsLen || need > bitsLen - off) return fail(why, "font glyph runs past the end of the file");
    }
    out.h = (uint8_t)h;
    out.ascent = d[6];
    out.meta = meta;
    out.bits = d + table;
    return true;
}

int advance(const Font& f, char c) {
    const uint8_t* e = entry(f, c);
    return e ? e[0] : 0;
}

bool pixel(const Font& f, char c, int x, int y) {
    const uint8_t* e = entry(f, c);
    if (!e || x < 0 || y < 0 || x >= e[1] || y >= f.h) return false;
    const int stride = (e[1] + 7) / 8;
    const size_t off = (size_t)e[2] | ((size_t)e[3] << 8);
    return (f.bits[off + (size_t)y * stride + (x >> 3)] >> (7 - (x & 7))) & 1;
}

int width(const Font& f, const char* s, int scale) {
    if (!s || !*s) return 0;
    int w = 0, gap = 0;
    for (; *s; ++s) {
        const uint8_t* e = entry(f, *s);
        if (!e) { gap = 0; continue; }
        w += e[0];
        gap = e[0] > e[1] ? e[0] - e[1] : 0;
    }
    w -= gap;
    return w < 0 ? 0 : w * scale;
}

}  // namespace fontpack
