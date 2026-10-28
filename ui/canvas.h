// The framebuffer every screen draws into.
//
// 160x120 at 16bpp is 38,400 bytes, which fits in the C5's internal SRAM with
// room to spare -- the point being that it does NOT have to live in PSRAM. The
// panel is 320x240, so the device blits this 1:2 and the chunky pixel look is
// a side effect of the memory decision rather than an affectation.
//
// It also matters that the C5 is single-core: packet capture and drawing share
// one CPU, so a full-screen redraw costs captures. Keeping the buffer small
// and the blit dumb is what makes a redraw cheap enough to afford.
#pragma once
#include <stdint.h>

namespace ui {

// The LANDSCAPE canvas, and the size of the buffer in either orientation.
// Every landscape screen draws against these constants, unchanged.
constexpr int W = 160;
constexpr int H = 120;

class Canvas {
public:
    uint16_t px[W * H];

    // The geometry actually in use: 160x120 (landscape, the default) or
    // 120x160 (portrait). The buffer is the same 19,200 pixels; `w` is the
    // stride and every helper clips to w x h. Change it with setSize().
    int16_t w = W, h = H;

    // Refuses (returns false, unchanged) a size that is not positive or does
    // not fit the buffer.
    bool setSize(int nw, int nh) {
        if (nw <= 0 || nh <= 0 || nw * nh > W * H) return false;
        w = (int16_t)nw; h = (int16_t)nh;
        return true;
    }

    void clear(uint16_t c);
    // Inline, with the clip read once into locals: this is the hottest call
    // in the UI (circles, lines, runes, glyphs, the emblem all land here).
    // Note px is uint16_t and w/h int16_t, which may alias, so a helper that
    // loops must copy w/h to locals first or every store reloads them.
    void pixel(int x, int y, uint16_t c) {
        const int cw = w, ch = h;
        if ((unsigned)x >= (unsigned)cw || (unsigned)y >= (unsigned)ch) return;
        px[y * cw + x] = c;
    }

    void hline(int x, int y, int w, uint16_t c);
    void vline(int x, int y, int h, uint16_t c);
    void line(int x0, int y0, int x1, int y1, uint16_t c);
    void rect(int x, int y, int w, int h, uint16_t c);      // outline
    void fill(int x, int y, int w, int h, uint16_t c);
    void circle(int cx, int cy, int r, uint16_t c);         // outline
    void disc(int cx, int cy, int r, uint16_t c);           // filled

    // Dotted outline: `on` pixels drawn, `off` skipped. For ghost frames and
    // range rings, where a solid line would read as structure.
    void dottedRect(int x, int y, int w, int h, uint16_t c, int on = 1, int off = 1);
    void dottedCircle(int cx, int cy, int r, uint16_t c, int step = 2);

    // Text. `scale` multiplies both axes; the pen advances Font::ADV*scale.
    void text(int x, int y, const char* s, uint16_t c, int scale = 1);
    void textRight(int xRight, int y, const char* s, uint16_t c, int scale = 1);
    void textCenter(int xCenter, int y, const char* s, uint16_t c, int scale = 1);

    // Darken every second row by half. A cheap CRT tell, applied last so it
    // sits over everything including text.
    void scanlines();

    // Fall off toward the corners, as if the only light is in the middle of
    // the circle. Applied before scanlines; both are whole-frame passes.
    void vignette();

    // Mix `a` toward `b` by n/d, in RGB565. Used for glow and fades; exact
    // enough at this bit depth that nobody will see the rounding.
    static uint16_t mix(uint16_t a, uint16_t b, int n, int d);
};

} // namespace ui
