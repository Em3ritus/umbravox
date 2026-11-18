// The SCAN screen's slice loop, shared by the board (device/engine_adapter.cpp) and the designer's speed meter
// (ui/meter.h): which parts of the rows [y0,y1) are composed for the pending frame, and how many pixels that is.
//
// For each 16-row strip it composes, in order of preference: the engine's dirty REGIONS that touch the strip (wide
// panels), else the radar's column WINDOW (a sweep-only frame), else the whole strip. Regions and the window are
// valid only against a panel mirror (`mirrorValid`): without one the whole strip is repainted.
//
// Stage receives what was composed:
//   full(sy, buf, sh)                    the whole width of rows [sy, sy+sh)
//   window(sy, buf, sh, lo, w)           columns [lo, lo+w), packed in buf
//   region(sy, buf, sh, lo, w, id)       one dirty region's columns, packed; id = its index in the engine's list
#pragma once
#include "scan_present.h"
#include "scan_render.h"
#include <cstdint>
#include <cstdio>

namespace uimeter {
// Pixels composed (drawn) and sent to the panel (pushed) for one frame.
struct Count {
    uint32_t drawn = 0, pushed = 0;
    void clear() { drawn = pushed = 0; }
};
} // namespace uimeter

namespace scanframe {

template <class Stage>
void slice(const scanpres::Engine& e, const scanr::ScanScene& sc, int W, int H, int y0, int y1, uint16_t* buf,
           bool mirrorValid, Stage& stage, uimeter::Count& n) {
    uieng::Rect rg[scanpres::Engine::kMaxRegions];
    const int nr = e.regions(rg, scanpres::Engine::kMaxRegions);
    int wlo = 0, whi = -1;
    if (nr <= 0 && !e.window(wlo, whi)) { wlo = 0; whi = -1; }
    const bool win = nr <= 0 && whi > wlo && mirrorValid;
    for (int sy = y0; sy < y1; sy += 16) {
        const int sh = (sy + 16 > y1) ? (y1 - sy) : 16;
        if (nr > 0 && mirrorValid) {
            for (int i = 0; i < nr; ++i) {
                if (rg[i].y >= sy + sh || rg[i].y + rg[i].h <= sy) continue;
                const int lo = rg[i].x < 0 ? 0 : rg[i].x, hi = rg[i].x + rg[i].w > W ? W : rg[i].x + rg[i].w;
                if (hi <= lo) continue;
                scanr::S s{ buf, W, sy, sh, lo, hi };          // buf holds just this region's columns, packed
                scanr::composeStrip(s, W, H, sc);
                n.drawn += (uint32_t)(hi - lo) * (uint32_t)sh;
                stage.region(sy, buf, sh, lo, hi - lo, i);
            }
        } else if (win) {
            scanr::S s{ buf, W, sy, sh, wlo, whi };
            scanr::composeStrip(s, W, H, sc);
            n.drawn += (uint32_t)(whi - wlo) * (uint32_t)sh;
            stage.window(sy, buf, sh, wlo, whi - wlo);
        } else {
            scanr::S s{ buf, W, sy, sh };
            scanr::composeStrip(s, W, H, sc);
            n.drawn += (uint32_t)W * (uint32_t)sh;
            stage.full(sy, buf, sh);
        }
    }
}

// One finished frame as the board's FRAMELOG console line (parsed by tools/meterprobe.py): pixels drawn and pushed,
// the frame's compose+compare+transfer time in us, the ms since the previous frame, and the shown alert's
// confidence (-1: none).
inline int formatFrm(char* out, size_t n, uint32_t d, uint32_t p, uint32_t us, uint32_t gap, int a) {
    return std::snprintf(out, n, "[frm] d %lu p %lu us %lu gap %lu a %d", (unsigned long)d, (unsigned long)p,
                         (unsigned long)us, (unsigned long)gap, a);
}

} // namespace scanframe
