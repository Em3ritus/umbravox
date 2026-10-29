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

// --- hit regions (id + rect + action), derived from the same geometry --------
// Rects are the layout regions above; ids/actions are the interaction contract
// (matches geometry.json "hits"). Fixed-capacity, no heap — embedded-friendly.
struct Hit { const char* id; Rect rect; const char* action; };
struct HitList {
    Hit items[20];
    int count = 0;
    void add(const char* id, Rect r, const char* action) { items[count++] = {id, r, action}; }
};

inline void addChromeHits(HitList& h, const Rect sc[4], const Rect nv[4]) {
    static const char* const sid[4]  = {"gps", "mesh", "power", "logging"};
    static const char* const sact[4] = {"Open GPS diagnostics", "Open Mesh",
                                         "Power detail (NM: sensing unavailable)",
                                         "Toggle logging; apply existing logger behavior"};
    static const char* const nid[4]  = {"nav-scan", "nav-domain", "nav-codex", "nav-menu"};
    static const char* const nact[4] = {"Open SCAN", "Open DOMAIN", "Open CODEX", "Open MENU"};
    for (int i = 0; i < 4; i++) h.add(sid[i], sc[i], sact[i]);
    for (int i = 0; i < 4; i++) h.add(nid[i], nv[i], nact[i]);
}

inline HitList scanHits(int W, int H) {            // 11 hits
    const ScanLayout s = scan(W, H);
    HitList h;
    addChromeHits(h, s.statusCells, s.navCells);
    static const char* const tid[3]  = {"WIFI", "5 GHZ", "BLE"};
    static const char* const tact[3] = {"Open Codex filtered to WIFI",
                                         "Open Codex filtered to 5 GHZ",
                                         "Open Codex filtered to BLE"};
    for (int i = 0; i < 3; i++) h.add(tid[i], s.tiles[i], tact[i]);
    return h;
}

// DOMAIN contacts (wide) tile into 44px bands: band 0 = located-scope header,
// bands 1..n-1 = mapped-device rows (opened by stable identity).
inline int  domainRowBands(const DomainLayout& d) { return d.contacts.h / 44; }
inline Rect domainBand(const DomainLayout& d, int k) {
    return {d.contacts.x, d.contacts.y + k * 44, d.contacts.w, 44};
}
inline HitList domainHits(int W, int H) {
    const DomainLayout d = domain(W, H);
    HitList h;
    addChromeHits(h, d.statusCells, d.navCells);
    h.add("range-minus", d.minus, "Select adjacent existing range; clamp at endpoint");
    h.add("range-plus",  d.plus,  "Select adjacent existing range; clamp at endpoint");
    if (d.wide) {
        const int n = domainRowBands(d);
        h.add("all-contacts", domainBand(d, 0), "Open Codex with located scope");
        for (int k = 1; k < n; k++)
            h.add("row", domainBand(d, k), "Open mapped device detail by stable identity");
    }
    return h;
}

}  // namespace uieng
