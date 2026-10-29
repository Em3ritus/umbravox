// layout_engine.h — resolution-independent UI geometry for umbravox.
//
// layout(W,H) rules compute every region as a pure function of the logical
// canvas size; hit regions come from the same geometry. One codebase lays out
// at any size; a scaling display adapter maps logical -> physical.
//
// Layout is pure geometry and state-INDEPENDENT. The ACTIVE HIT SET is
// state-AWARE: a threat target appears only in the threat state, and DOMAIN
// rows need a GPS fix and located records. Rects reproduce Codex's
// geometry.json EXACTLY for every state at both approved sizes (480x320,
// 320x240) -- the host oracle test asserts the complete set. No Arduino deps.
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
    bool wide = false;   // contacts shown as a list; else collapse to a summary
};
inline DomainLayout domain(int W, int H) {
    const Chrome c = chrome(W, H);
    DomainLayout d;
    d.status = c.status; d.nav = c.nav; d.body = c.body;
    for (int i = 0; i < 4; i++) { d.statusCells[i] = c.statusCells[i]; d.navCells[i] = c.navCells[i]; }

    const int bw = c.body.w, bh = c.body.h;
    d.wide = bw >= 352;                                    // existing collapse threshold
    // Delivered rule: wide uses a 144px floor on 0.30*bodyW; compact is a fixed
    // 112px summary column (reviewer-corrected; both approved sizes match).
    const int contactsW = d.wide ? std::max(144, iround(0.30 * bw)) : 112;
    const int mapW = bw - contactsW - 8;
    d.map      = {c.body.x, c.body.y, mapW, bh};
    d.contacts = {c.body.x + mapW + 8, c.body.y, contactsW, bh - 48};
    const int ry = c.body.y + bh - 44;
    d.range = {d.contacts.x, ry, contactsW, 44};
    d.minus = {d.contacts.x, ry, 44, 44};
    d.plus  = {d.contacts.x + contactsW - 44, ry, 44, 44};
    return d;
}

// --- hit regions: slot id + rect + action + payload, from the same geometry ---
// The `desc` is documentation; `action`+`payload`/`key` drive runtime dispatch.
enum class Action : int { OpenGps, OpenMesh, PowerDetail, ToggleLogging,
                          Nav, CodexFilter, RangeStep,
                          OpenLocatedCodex, OpenDeviceDetail, OpenThreatDetail };
enum class ScreenId : int { Scan, Domain, Codex, Menu };   // Nav payload
enum class Band     : int { Wifi, FiveGhz, Ble };          // CodexFilter payload

struct Hit {
    const char* slot;    // distinct within a screen's hit list
    Rect        rect;
    Action      action;
    int         payload; // Nav->ScreenId, CodexFilter->Band, RangeStep->+/-1, row->index
    const char* key;     // stable record key for row/threat targets, else nullptr
    const char* desc;    // human-readable (docs only)
};
struct HitList {
    Hit items[20];
    int count = 0;
    void add(const char* slot, Rect r, Action a, int payload, const char* key, const char* desc) {
        items[count++] = {slot, r, a, payload, key, desc};
    }
};

// Presentation state the ACTIVE hit set depends on. Row keys are a snapshot the
// caller freezes during a touch so the row<->record binding can't shift.
struct HitCtx {
    bool threat   = false;   // a signature match is live
    bool hasFix   = true;    // GPS fix available (DOMAIN maps located rows)
    int  nLocated = 99;      // located records available to map (DOMAIN wide)
    const char*        threatKey = "threat";
    const char* const* rowKeys   = nullptr;   // distinct stable keys, caller-owned
    int                nRowKeys  = 0;
};

static const char* const kThreatDesc =
    "Open matching device detail; retain confidence and evidence; not a confirmed identity";

inline void addChromeHits(HitList& h, const Rect sc[4], const Rect nv[4]) {
    h.add("gps",     sc[0], Action::OpenGps,       0, nullptr, "Open GPS diagnostics");
    h.add("mesh",    sc[1], Action::OpenMesh,      0, nullptr, "Open Mesh");
    h.add("power",   sc[2], Action::PowerDetail,   0, nullptr, "Power detail (NM: sensing unavailable)");
    h.add("logging", sc[3], Action::ToggleLogging, 0, nullptr, "Toggle logging; apply existing logger behavior");
    static const char* const nslot[4] = {"nav-scan","nav-domain","nav-codex","nav-menu"};
    static const char* const ndesc[4] = {"Open SCAN","Open DOMAIN","Open CODEX","Open MENU"};
    for (int i = 0; i < 4; i++) h.add(nslot[i], nv[i], Action::Nav, i, nullptr, ndesc[i]);
}

inline HitList scanHits(int W, int H, const HitCtx& ctx) {
    const ScanLayout s = scan(W, H);
    HitList h;
    addChromeHits(h, s.statusCells, s.navCells);
    static const char* const tslot[3] = {"WIFI","5 GHZ","BLE"};
    static const char* const tdesc[3] = {"Open Codex filtered to WIFI",
                                         "Open Codex filtered to 5 GHZ",
                                         "Open Codex filtered to BLE"};
    for (int i = 0; i < 3; i++) h.add(tslot[i], s.tiles[i], Action::CodexFilter, i, nullptr, tdesc[i]);
    if (ctx.threat)
        h.add("threat", {s.hero.x, s.hero.y, s.hero.w, 44},
              Action::OpenThreatDetail, 0, ctx.threatKey, kThreatDesc);
    return h;
}

inline int domainRowBands(const DomainLayout& d) { return d.contacts.h / 44; }

inline HitList domainHits(int W, int H, const HitCtx& ctx) {
    const DomainLayout d = domain(W, H);
    HitList h;
    addChromeHits(h, d.statusCells, d.navCells);
    h.add("range-minus", d.minus, Action::RangeStep, -1, nullptr, "Select adjacent existing range; clamp at endpoint");
    h.add("range-plus",  d.plus,  Action::RangeStep, +1, nullptr, "Select adjacent existing range; clamp at endpoint");

    const int B = 44;
    const int cx = d.contacts.x, cy = d.contacts.y, cw = d.contacts.w;
    static const char* const ROWSLOT[8] = {"row0","row1","row2","row3","row4","row5","row6","row7"};

    if (d.wide) {
        const int nbands = domainRowBands(d);                 // band 0 = header, 1.. = rows
        h.add("all-contacts", {cx, cy, cw, B}, Action::OpenLocatedCodex, 0, nullptr,
              "Open Codex with located scope");
        int b = 1;
        if (ctx.threat && b < nbands) {
            h.add("threat", {cx, cy + b*B, cw, B}, Action::OpenThreatDetail, 0, ctx.threatKey, kThreatDesc);
            ++b;
        }
        for (int ri = 0; b < nbands && ctx.hasFix && ri < ctx.nLocated; ++b, ++ri) {
            const char* key = (ctx.rowKeys && ri < ctx.nRowKeys) ? ctx.rowKeys[ri] : ROWSLOT[ri & 7];
            h.add(key, {cx, cy + b*B, cw, B}, Action::OpenDeviceDetail, ri, key,
                  "Open mapped device detail by stable identity");
        }
    } else if (ctx.threat) {
        h.add("threat", {cx, cy, cw, B}, Action::OpenThreatDetail, 0, ctx.threatKey, kThreatDesc);
        h.add("all-contacts", {cx, cy + B, cw, B}, Action::OpenLocatedCodex, 0, nullptr, "Open located Codex");
    } else {
        h.add("all-contacts", d.contacts, Action::OpenLocatedCodex, 0, nullptr, "Open Codex with located scope");
    }
    return h;
}

}  // namespace uieng
