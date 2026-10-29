// layout_engine.h — resolution-independent UI geometry for umbravox.
//
// The engine replaces hardcoded pixel grids with layout(W, H) rules: every
// region is a pure function of the logical canvas size, and hit regions come
// from the same geometry. One codebase lays out at any size; a scaling display
// adapter maps logical -> physical (see the UI engine contract).
//
// Rules here reproduce Codex's exported geometry.json EXACTLY for the two
// approved profiles (480x320, 320x240); the host test asserts that. No Arduino
// deps -- compiles on host and device.
#pragma once
#include <algorithm>
#include <cmath>

namespace uieng {

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool operator==(const Rect& o) const { return x==o.x && y==o.y && w==o.w && h==o.h; }
};

inline int iround(double v) { return (int)std::lround(v); }

// --- shared chrome: status bar (top), nav bar (bottom), body, 4 cells each ----
struct Chrome {
    Rect status, nav, body;
    Rect statusCells[4], navCells[4];
};
inline Chrome chrome(int W, int H) {
    Chrome c;
    c.status = {0, 0, W, 44};
    c.nav    = {0, H - 44, W, 44};
    c.body   = {8, 52, W - 16, H - 104};   // 52 = 44 status + 8 gap; 104 = that + 44 nav + 8 gap
    const int cw = W / 4;
    for (int i = 0; i < 4; i++) {
        c.statusCells[i] = {i * cw, 0, cw, 44};
        c.navCells[i]    = {i * cw, H - 44, cw, 44};
    }
    return c;
}

// --- SCAN: counts rail (3 tiles) + hero (LIVE FIELD) --------------------------
struct ScanLayout {
    Rect status, nav, body, statusCells[4], navCells[4];
    Rect counts, hero, tiles[3];
};
inline ScanLayout scan(int W, int H) {
    const Chrome c = chrome(W, H);
    ScanLayout s;
    s.status = c.status; s.nav = c.nav; s.body = c.body;
    for (int i = 0; i < 4; i++) { s.statusCells[i] = c.statusCells[i]; s.navCells[i] = c.navCells[i]; }

    const int bw = c.body.w, bh = c.body.h;
    const int countsW = std::max(88, iround(0.24 * bw));   // proportional, 88px floor (reviewer-verified)
    s.counts = {c.body.x, c.body.y, countsW, bh};
    const int th = bh / 3;                                 // integer division; last tile takes remainder
    for (int i = 0; i < 3; i++) {
        const int hh = (i < 2) ? th : bh - 2 * th;
        s.tiles[i] = {c.body.x, c.body.y + i * th, countsW, hh};
    }
    s.hero = {c.body.x + countsW + 8, c.body.y, bw - countsW - 8, bh};
    return s;
}

// --- DOMAIN: rectangular map (radar centered inside) + contacts + range -------
struct DomainLayout {
    Rect status, nav, body, statusCells[4], navCells[4];
    Rect map, contacts, range, minus, plus;
    bool wide = false;   // contacts list shown; else collapse to summary
};
inline DomainLayout domain(int W, int H) {
    const Chrome c = chrome(W, H);
    DomainLayout d;
    d.status = c.status; d.nav = c.nav; d.body = c.body;
    for (int i = 0; i < 4; i++) { d.statusCells[i] = c.statusCells[i]; d.navCells[i] = c.navCells[i]; }

    const int bw = c.body.w, bh = c.body.h;
    d.wide = bw >= 352;                                    // existing collapse threshold
    // NOTE: the contacts proportion is UNDER-DETERMINED by two profiles (the
    // 320 value is floored to 112 either way). 0.30*W reproduces both; confirm
    // the exact rule against Codex's generator before trusting other sizes.
    const int contactsW = std::max(112, iround(0.30 * W));
    const int mapW = bw - contactsW - 8;
    d.map      = {c.body.x, c.body.y, mapW, bh};
    d.contacts = {c.body.x + mapW + 8, c.body.y, contactsW, bh - 48};
    const int ry = c.body.y + bh - 44;
    d.range = {d.contacts.x, ry, contactsW, 44};
    d.minus = {d.contacts.x, ry, 44, 44};
    d.plus  = {d.contacts.x + contactsW - 44, ry, 44, 44};
    return d;
}

}  // namespace uieng
