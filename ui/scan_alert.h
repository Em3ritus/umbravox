// Alert takeover for the wide engine panel. Included by scan_render.h (inside a re-opened namespace scanr), so it
// can use that file's strip primitives. Integer maths only (the C5 has no FPU).
//
// LOW/MED confidence take the hero area, HIGH takes the whole screen with a longer, louder version of the same rite.
// The rite follows the theme exactly as on the CYD: CVLT summons, the muffin plays its chosen style, packs name theirs,
// everything else traces the ward. Colour is the confidence. The threat name resolves out of runes, then the detail arrives.
#pragma once
#include "contrast.h"

namespace scanr {

enum Rite : uint8_t { RITE_SUPERNOVA = 0, RITE_VORTEX, RITE_COMET, RITE_LOCKON, RITE_WARD, RITE_SUMMON };
inline Rite riteFor(uint8_t theme, uint8_t style) {
    if (theme == pal::TH_BAPH0MAU5) return RITE_SUMMON;
    if (theme == pal::TH_COSMICMUFFIN) return style == ui::AL_VORTEX ? RITE_VORTEX : style == ui::AL_COMET ? RITE_COMET : style == ui::AL_LOCKON ? RITE_LOCKON : RITE_SUPERNOVA;
    if (theme >= pal::TH_SLICK) {
        switch (pal::packRite(theme)) {
            case themepack::RITE_SUPERNOVA: return RITE_SUPERNOVA; case themepack::RITE_VORTEX: return RITE_VORTEX;
            case themepack::RITE_COMET: return RITE_COMET;         case themepack::RITE_LOCKON: return RITE_LOCKON;
            default: break;
        }
    }
    return RITE_WARD;
}

// Clip rectangle (the takeover) inside the strip's row/column window.
struct Rc { S& s; int X, Y, W, H; };
inline void apx(Rc& r, int x, int y, uint16_t col, int lum = 255) {
    if (x < r.X || x >= r.X + r.W || y < r.Y || y >= r.Y + r.H || x < r.s.lo || x >= r.s.hi || y < r.s.y0 || y >= r.s.y0 + r.s.h) return;
    uint16_t& d = r.s.px[(y - r.s.y0) * r.s.stride + (x - r.s.lo)];
    d = lum >= 255 ? col : ui::Canvas::mix(d, col, lum, 255);
}
inline void afill(Rc& r, int x, int y, int w, int h, uint16_t col, int lum = 255) {
    const int x0 = std::max({x, r.X, r.s.lo}), x1 = std::min({x + w, r.X + r.W, r.s.hi});
    const int y0 = std::max({y, r.Y, r.s.y0}), y1 = std::min({y + h, r.Y + r.H, r.s.y0 + r.s.h});
    for (int yy = y0; yy < y1; yy++) {
        uint16_t* row = r.s.px + (yy - r.s.y0) * r.s.stride - r.s.lo;
        if (lum >= 255) for (int xx = x0; xx < x1; xx++) row[xx] = col;
        else for (int xx = x0; xx < x1; xx++) row[xx] = ui::Canvas::mix(row[xx], col, lum, 255);
    }
}
inline void adot(Rc& r, int x, int y, int th, uint16_t col, int lum = 255) {
    if (x + th <= std::max(r.X, r.s.lo) || x >= std::min(r.X + r.W, r.s.hi) || y + th <= std::max(r.Y, r.s.y0) || y >= std::min(r.Y + r.H, r.s.y0 + r.s.h)) return;
    for (int j = 0; j < th; j++) for (int i = 0; i < th; i++) apx(r, x + i, y + j, col, lum);
}
inline void aline(Rc& r, int x0, int y0, int x1, int y1, uint16_t col, int lum, int th) {
    const int wx0 = std::max(r.X, r.s.lo) - th, wx1 = std::min(r.X + r.W, r.s.hi), wy0 = std::max(r.Y, r.s.y0) - th, wy1 = std::min(r.Y + r.H, r.s.y0 + r.s.h);
    if ((x0 < wx0 && x1 < wx0) || (x0 >= wx1 && x1 >= wx1) || (y0 < wy0 && y1 < wy0) || (y0 >= wy1 && y1 >= wy1)) return;   // segment wholly outside this strip/window
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0, sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        adot(r, x0, y0, th, col, lum);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
// A ring, row by row: each row of the strip draws the one or two runs where the circle's band crosses it, so a strip
// only pays for its own rows (no walk round the whole circle per strip).
inline void aring(Rc& r, int cx, int cy, int rad, uint16_t col, int lum, int th = 1) {
    if (rad < 1) return;
    const int ya = std::max({cy - rad, r.Y, r.s.y0}), yb = std::min({cy + rad, r.Y + r.H - 1, r.s.y0 + r.s.h - 1});
    const int in = std::max(0, rad - th);
    for (int y = ya; y <= yb; y++) {
        const int dy = y - cy, outer = isqrt(rad * rad - dy * dy);
        const int inner2 = in * in - dy * dy, inner = inner2 >= 0 ? isqrt(inner2) : -1;
        if (inner < 0) afill(r, cx - outer, y, 2 * outer + 1, 1, col, lum);
        else { afill(r, cx - outer, y, outer - inner, 1, col, lum); afill(r, cx + inner + 1, y, outer - inner, 1, col, lum); }
    }
}
inline void adisc(Rc& r, int cx, int cy, int rad, uint16_t col, int lum = 255) {
    for (int y = cy - rad; y <= cy + rad; y++) { if (y < r.s.y0 || y >= r.s.y0 + r.s.h) continue; const int hw = isqrt(rad * rad - (y - cy) * (y - cy)); afill(r, cx - hw, y, 2 * hw + 1, 1, col, lum); }
}
inline void aframe(Rc& r, int th, uint16_t col, int lum = 255) {
    afill(r, r.X, r.Y, r.W, th, col, lum); afill(r, r.X, r.Y + r.H - th, r.W, th, col, lum);
    afill(r, r.X, r.Y + th, th, r.H - 2 * th, col, lum); afill(r, r.X + r.W - th, r.Y + th, th, r.H - 2 * th, col, lum);
}
inline uint32_t anoise(uint32_t a, uint32_t b) { uint32_t h = a * 374761393u + b * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; return h ^ (h >> 16); }
// A rune from the ring's alphabet, centred, `sc` pixels per cell.
inline void arune(Rc& r, int k, int cx, int cy, int sc, uint16_t col, int lum = 255) {
    const uint8_t* g = ui::runeBits(k);
    for (int j = 0; j < ui::RUNE_S; j++) for (int i = 0; i < ui::RUNE_S; i++)
        if ((g[j] >> (ui::RUNE_S - 1 - i)) & 1) afill(r, cx - ui::RUNE_S * sc / 2 + i * sc, cy - ui::RUNE_S * sc / 2 + j * sc, sc, sc, col, lum);
}
// The border's perimeter walked from top-centre (clockwise for +d), as the CYD ward traces it.
inline int perimLen(int w, int h) { return (w - w / 2 - 1) + (h - 1) + (w - 1) + (h - 1) + w / 2; }
inline void perimPt(int x, int y, int w, int h, int d, int& px, int& py) {
    const int half = w / 2, segA = w - half - 1, segB = h - 1, segC = w - 1, segD = h - 1, total = segA + segB + segC + segD + half;
    d = ((d % total) + total) % total;
    if (d < segA) { px = x + half + d; py = y; return; } d -= segA;
    if (d < segB) { px = x + w - 1; py = y + d; return; } d -= segB;
    if (d < segC) { px = x + w - 1 - d; py = y + h - 1; return; } d -= segC;
    if (d < segD) { px = x; py = y + h - 1 - d; return; } d -= segD;
    px = x + d; py = y;
}
inline void starPts(int cx, int cy, int rad, int* px, int* py) { for (int k = 0; k < 5; k++) polarPt(cx, cy, rad, 512 + k * 1024 / 5, px[k], py[k]); }   // point down
inline void pentagram(Rc& r, int cx, int cy, int rad, uint16_t col, int lum, int th, int* qx = nullptr, int* qy = nullptr) {
    int px[5], py[5]; starPts(cx, cy, rad, px, py);
    for (int k = 0; k < 5; k++) aline(r, px[k], py[k], px[(k + 2) % 5], py[(k + 2) % 5], col, lum, th);
    if (qx) for (int k = 0; k < 5; k++) { qx[k] = px[k]; qy[k] = py[k]; }
}
// The pentagram drawn stroke by stroke; p = 0..1000. Hands back the pen tip.
inline void pentagramTrace(Rc& r, int cx, int cy, int rad, uint16_t col, int p, int th, int& tx, int& ty) {
    int px[5], py[5]; starPts(cx, cy, rad, px, py);
    const int seg = p >= 1000 ? 4 : p * 5 / 1000, fr = p >= 1000 ? 1000 : p * 5 % 1000;
    tx = px[0]; ty = py[0];
    for (int k = 0; k <= seg; k++) {
        const int a = k, b = (k + 2) % 5; const int ex = px[a] + (px[b] - px[a]) * (k == seg ? fr : 1000) / 1000, ey = py[a] + (py[b] - py[a]) * (k == seg ? fr : 1000) / 1000;
        aline(r, px[a], py[a], ex, ey, col, 255, th); tx = ex; ty = ey;
    }
}

struct Beat { bool hi; int flare, trace, charMs; };
inline Beat beatFor(bool hi) { return hi ? Beat{true, 200, 700, 80} : Beat{false, 90, 300, 55}; }

// Threat name resolving out of runes, then the supporting detail, shared by every rite.
inline void alertText(Rc& r, const AlertScene& a, uint16_t edge, int age, const Beat& b) {
    const int cx = r.X + r.W / 2;
    const int ns = fitScale(a.label, r.W - (b.hi ? 40 : 24), b.hi ? 7 : 3);
    const int ny = r.Y + (b.hi ? r.H * 24 / 100 : r.H * 17 / 100);
    arune(r, 0, cx, ny - (b.hi ? 20 : 12), b.hi ? 4 : 2, edge);                      // the eye
    int n = 0; while (a.label[n]) n++;
    const int startX = cx - Font::width(a.label, ns) / 2, settle = b.trace;
    for (int i = 0; i < n; i++) {
        const int gx = startX + i * Font::ADV * ns, lockAt = settle + i * b.charMs;
        if (age >= lockAt) { const char ch[2] = { a.label[i], 0 }; if (ny + Font::H * ns > r.s.y0 && ny < r.s.y0 + r.s.h) txt(r.s, gx, ny, ch, ns, pal::BRIGHT); }
        else arune(r, int(anoise(uint32_t(age / 70), uint32_t(i)) % 16), gx + Font::W * ns / 2, ny + Font::H * ns / 2, std::max(1, ns / 2), edge);
    }
    const int ds = b.hi ? 3 : 2, tail = settle + n * b.charMs, y1 = ny + Font::H * ns + (b.hi ? 14 : 8), step = Font::H * ds + (b.hi ? 8 : 5);
    if (age > tail)       centered(r.s, cx, y1, a.why, ds, pal::DIM);
    if (age > tail + 80 && a.name[0]) centered(r.s, cx, y1 + step, a.name, fitScale(a.name, r.W - 16, ds), pal::TEXT);
    if (age > tail + 160) centered(r.s, cx, y1 + 2 * step, a.conf == ui::CONF_HIGH ? "HIGH" : a.conf == ui::CONF_MED ? "MED" : "LOW", ds, edge);
}

inline void riteMotif(Rc& r, uint8_t theme, uint16_t edge, int mx, int my, int rm, int opacity) {
    if (opacity > 100) opacity = 100; if (opacity < 0) opacity = 0;
    const themepack::Emblem* e = pal::emblem(theme);
    if (e && pal::centrepiece(theme) == pal::CP_EMBLEM) emblemInto(r.s, *e, mx, my, rm, false, opacity);
    else { const uint16_t c = ui::Canvas::mix(pal::VOID, edge, opacity, 100); heptaInto(r.s, mx, my, rm, c, c); }
}
inline void flash(Rc& r, int since, int len, bool hi) { if (since >= 0 && since < len) afill(r, r.X, r.Y, r.W, r.H, ui::Canvas::mix(pal::VOID, pal::BRIGHT, len - since, len * 3 / 2)); (void)hi; }

inline void riteWard(Rc& r, const ScanScene& sc, uint16_t edge, int age, const Beat& b, int mx, int my, int R) {
    const int th = b.hi ? 3 : 2;
    if (age < b.flare) {                                     // the strike: the figure arrives oversized and contracts onto the card
        const int k = b.flare - age;
        afill(r, r.X, r.Y, r.W, r.H, ui::Canvas::mix(pal::VOID, edge, k, b.flare * 2));
        const uint16_t c = ui::Canvas::mix(edge, pal::BRIGHT, k, b.flare);
        heptaInto(r.s, mx, my, R * 55 / 100 + R * k / (b.flare * 2), c, c);
        return;
    }
    { const uint16_t c = ui::Canvas::mix(pal::VOID, edge, 1, 5); heptaInto(r.s, mx, my, R * 55 / 100, c, c); }
    const int plen = perimLen(r.W, r.H), drawn = age >= b.trace ? plen : plen * age / b.trace;
    if (age >= b.trace) aframe(r, th, edge);                 // the ward has closed: the whole border is one frame (no per-pixel walk per strip)
    else for (int d = 0; d <= drawn / 2; d++) { int px, py; perimPt(r.X, r.Y, r.W, r.H, d, px, py); adot(r, px, py, th, edge); perimPt(r.X, r.Y, r.W, r.H, -d, px, py); adot(r, px, py, th, edge); }
    if (age >= b.trace) {
        const int since = age - b.trace;
        if (since < 120) { const int k = 120 - since; const uint16_t f = ui::Canvas::mix(edge, pal::BRIGHT, k, 120); adisc(r, r.X + r.W / 2, r.Y, 2 + k / 20, f); }
        const uint32_t fl = anoise(uint32_t(age / 90), 7) % 24;
        aframe(r, th + 1, ui::Canvas::mix(pal::VOID, edge, 14 + int(fl), 60));
        for (int i = 0; i < 8; i++) { int px, py; perimPt(r.X, r.Y, r.W, r.H, age / 30 + i * plen / 8, px, py); arune(r, i + age / 900, px, py, b.hi ? 3 : 2, ui::Canvas::mix(pal::VOID, edge, 2, 3)); }
    }
}
inline void riteSummon(Rc& r, const ScanScene& sc, uint16_t edge, int age, const Beat& b, int mx, int my, int R) {
    const int conf = sc.alert.conf, th = b.hi ? 3 : 2, rad = R - 6;
    const uint16_t core = conf == ui::CONF_HIGH ? ui::Canvas::mix(pal::RED, pal::LIVE_HI, 1, 2) : conf == ui::CONF_MED ? ui::Canvas::mix(pal::AMBER, pal::LIVE_HI, 1, 2) : ui::Canvas::mix(pal::WITCH, pal::LIVE, 1, 3);
    const int heatN = conf == ui::CONF_HIGH ? 5 : conf == ui::CONF_MED ? 3 : 2;
    if (age < b.flare) {
        const int k = b.flare - age;
        afill(r, r.X, r.Y, r.W, r.H, ui::Canvas::mix(pal::VOID, edge, k, b.flare * 2));
        pentagram(r, mx, my, rad + rad * k / (3 * b.flare), ui::Canvas::mix(edge, pal::BRIGHT, k, b.flare), 255, th);
        return;
    }
    const bool held = age >= b.trace; const int fade = held ? 2 : 1;
    const int br = !held ? (rad + 6) * (age - b.flare) / (b.trace - b.flare) : rad / 2 + int(anoise(uint32_t(age / 80), 11) % 7) - 3;
    if (br > 0) {
        adisc(r, mx, my, br, ui::Canvas::mix(pal::VOID, edge, heatN, 10 * fade));
        adisc(r, mx, my, br * 2 / 3, ui::Canvas::mix(pal::VOID, edge, heatN, 6 * fade));
        adisc(r, mx, my, br / 3, ui::Canvas::mix(pal::VOID, core, 1, fade));
    }
    if (!held) {
        pentagram(r, mx, my, rad, ui::Canvas::mix(pal::VOID, pal::WITCH, 1, 6), 255, th);
        int tx, ty; pentagramTrace(r, mx, my, rad, pal::WITCH_HI, (age - b.flare) * 1000 / (b.trace - b.flare), th, tx, ty);
        adisc(r, tx, ty, th, pal::LIVE_HI);
    } else {
        int qx[5], qy[5];
        pentagram(r, mx, my, rad, ui::Canvas::mix(pal::VOID, pal::WITCH, 1, 3), 255, th, qx, qy);
        aring(r, mx, my, rad, ui::Canvas::mix(pal::VOID, pal::WITCH, 1, 2), 255, th);
        for (int k = 0; k < 5; k++) { const int fl = int(anoise(uint32_t(age / 90), uint32_t(k)) % 20); adisc(r, qx[k], qy[k], th, ui::Canvas::mix(edge, pal::LIVE_HI, 20 + fl, 44)); }
        adisc(r, qx[0], qy[0], th + 1, pal::LIVE_HI);
        const uint32_t fl = anoise(uint32_t(age / 90), 7) % 24;
        aframe(r, th, ui::Canvas::mix(pal::VOID, edge, 14 + int(fl), 60));
    }
}
inline void riteSupernova(Rc& r, const ScanScene& sc, uint16_t edge, int age, const Beat& b, int mx, int my, int R) {
    const int rm = R * 45 / 100, th = b.hi ? 3 : 2;
    if (age < b.flare) { riteMotif(r, sc.theme, edge, mx, my, rm, 60 + 40 * age / b.flare); return; }
    const int since = age - b.flare; flash(r, since, b.hi ? 260 : 160, b.hi);
    for (int k = 0; k < (b.hi ? 3 : 2); k++) {
        const int per = b.hi ? 1200 : 900, ph = (since + k * per / (b.hi ? 3 : 2)) % per, rr = 3 + ph * (R - 3) / per, lum = 255 - ph * 220 / per;
        aring(r, mx, my, rr, k ? pal::SWEEP : edge, lum, th);
        if (rr > 3) aring(r, mx, my, rr - th, pal::SWEEP_HI, lum / 2, 1);
    }
    riteMotif(r, sc.theme, edge, mx, my, rm, since < 200 ? 100 : 60);
    aframe(r, th, edge);
}
inline void riteVortex(Rc& r, const ScanScene& sc, uint16_t edge, int age, const Beat& b, int mx, int my, int R) {
    const bool held = age >= b.trace; const int th = b.hi ? 3 : 2, RR = R - 1;
    const int pull = held ? 450 : 1000 - 550 * age / b.trace;
    const int spin = held ? b.trace * 23 / 10 + (age - b.trace) * 13 / 20 : age * 23 / 10;
    const uint16_t arms[3] = { pal::SWEEP_HI, pal::SWEEP, pal::WITCH };
    for (int arm = 0; arm < 3; arm++) {
        int px = 0, py = 0;
        for (int i = 0; i < 22; i++) {
            const int t100 = 15 + 14 * i, f1000 = t100 * 1000 / 320;
            const int rad = RR * pull / 1000 * (120 + 880 * f1000 / 1000) / 1000;
            const int a = t100 * 391 / 100 + arm * 341 + spin;
            const int x = mx + icos(a) * rad / 1024, y = my + isin(a) * rad / 1024;
            if (i) aline(r, px, py, x, y, arms[arm], 250 - f1000 * 150 / 1000, th);
            px = x; py = y;
        }
    }
    adisc(r, mx, my, (held ? 4 : 2 + 3 * age / b.trace) * (b.hi ? 2 : 1), held ? pal::BRIGHT : pal::BONE);
    if (held) { flash(r, age - b.trace, b.hi ? 220 : 120, b.hi); aframe(r, th, edge); }
}
inline void riteComet(Rc& r, const ScanScene& sc, uint16_t edge, int age, const Beat& b, int mx, int my, int R) {
    const int rm = R * 45 / 100, T = b.trace, th = b.hi ? 3 : 2;
    riteMotif(r, sc.theme, edge, mx, my, rm, age < T ? 100 : 60);
    if (age < T) {
        const int sx = r.X + r.W - 3, sy = r.Y + 3, hx = sx + (mx - sx) * age / T, hy = sy + (my - sy) * age / T;
        const int dx = sx - mx, dy = sy - my, L = std::max(1, isqrt(dx * dx + dy * dy));
        for (int k = 16; k >= 1; k--) { const int tx = hx + dx * k * 13 / (10 * L) * (b.hi ? 2 : 1), ty = hy + dy * k * 13 / (10 * L) * (b.hi ? 2 : 1);
            adot(r, tx, ty, th, k < 6 ? pal::BONE : pal::SWEEP, 255 - k * 13); }
        adisc(r, hx, hy, th, pal::BRIGHT);
    } else {
        const int since = age - T; flash(r, since, b.hi ? 240 : 140, b.hi);
        const int per = b.hi ? 1400 : 1000, ph = since % per, rr = rm + ph * (R - rm) / per, lum = 255 - ph * 220 / per;
        aring(r, mx, my, rr, pal::SWEEP, lum, th);
        for (int i = 0; i < 8; i++) { const int d = rm + ph * (R - rm) * 9 / (per * 10); adot(r, mx + icos(i * 128 + 64) * d / 1024, my + isin(i * 128 + 64) * d / 1024, th, (i & 1) ? pal::BONE : pal::SWEEP_HI, lum); }
        aframe(r, th, edge);
    }
}
inline void riteLockOn(Rc& r, const ScanScene& sc, uint16_t edge, int age, const Beat& b, int mx, int my, int R) {
    const int rm = R * 45 / 100, T = b.trace, th = b.hi ? 3 : 2;
    riteMotif(r, sc.theme, edge, mx, my, rm, age < T ? 85 : 65);
    if (age < T) {
        const int span = R - rm - 3;
        for (int k = 0; k < 3; k++) { int rr = rm + 3 + span * (T - age) / T + k * 3 * (T - age) / T; if (rr > R) rr = R; aring(r, mx, my, rr, pal::SWEEP, 90 + 150 * age / T, th); }
        const int ang = age * 13 / 4;
        for (int j = 0; j < 4; j++) { const int a = ang + j * 256; aline(r, mx + icos(a) * (rm + 2) / 1024, my + isin(a) * (rm + 2) / 1024, mx + icos(a) * (rm + 8) / 1024, my + isin(a) * (rm + 8) / 1024, pal::SWEEP_HI, 255, th); }
    } else {
        const int since = age - T; flash(r, since, b.hi ? 200 : 100, b.hi);
        const int bb = rm + 4 + ((since / 250) & 1), len = b.hi ? 10 : 5;
        for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2) { const int x = mx + sx * bb, y = my + sy * bb;
            aline(r, x, y, x - sx * len, y, pal::SWEEP, 255, th); aline(r, x, y, x, y - sy * len, pal::SWEEP, 255, th); }
        aframe(r, th, edge);
    }
}

// 0 = no takeover, 1 = hero area (LOW/MED), 2 = whole screen (HIGH).
inline int alertLevel(const AlertScene& a) { return !a.active ? 0 : a.conf == ui::CONF_HIGH ? 2 : 1; }

// Where the rite lives inside the takeover rectangle (the same numbers drive drawing and the dirty regions).
struct AlertGeom { int mx, my, R; };
inline AlertGeom alertGeom(const uieng::Rect& area, bool hi) {
    return { area.x + area.w / 2, area.y + area.h / 2, hi ? std::min(area.w, area.h) * 36 / 100 : std::min(area.w, area.h) / 2 - 2 };
}
// After this age everything outside the rite square, the border band and (done resolving) the text is static: the
// opening flash/strobe/trace and the name resolving have finished.
inline bool alertSettled(const AlertScene& a) {
    const Beat b = beatFor(alertLevel(a) == 2); int n = 0; while (a.label[n]) n++;
    return int(a.age) >= std::max(b.trace + 300, b.trace + n * b.charMs + 260);
}
// The rectangles that can still change once the alert has settled: the rite square and four border bands.
inline int alertRegions(const uieng::Rect& area, const AlertScene& a, uint8_t theme, uieng::Rect* out, int cap) {
    if (cap < 5) { out[0] = area; return 1; }
    const bool hi = alertLevel(a) == 2; const AlertGeom g = alertGeom(area, hi);
    const int band = riteFor(theme, a.style) == RITE_WARD ? (hi ? 20 : 14) : 6;     // only the ward walks runes along the border; the rest draw a thin frame
    auto clip = [&](int x, int y, int w, int h) { const int x0 = std::max(x, area.x), y0 = std::max(y, area.y), x1 = std::min(x + w, area.x + area.w), y1 = std::min(y + h, area.y + area.h);
        return (x1 > x0 && y1 > y0) ? uieng::Rect{ x0, y0, x1 - x0, y1 - y0 } : uieng::Rect{ 0, 0, 0, 0 }; };
    int n = 0; uieng::Rect r;
    r = clip(g.mx - g.R - 3, g.my - g.R - 3, 2 * g.R + 7, 2 * g.R + 7); if (r.w) out[n++] = r;
    r = clip(area.x, area.y, area.w, band);                              if (r.w) out[n++] = r;
    r = clip(area.x, area.y + area.h - band, area.w, band);              if (r.w) out[n++] = r;
    r = clip(area.x, area.y + band, band, area.h - 2 * band);            if (r.w) out[n++] = r;
    r = clip(area.x + area.w - band, area.y + band, band, area.h - 2 * band); if (r.w) out[n++] = r;
    return n;
}

// Overlay the takeover rectangle `area` onto the strip.
inline void drawAlertTakeover(S& s, const ScanScene& sc, const uieng::Rect& area) {
    const AlertScene& a = sc.alert;
    if (area.y >= s.y0 + s.h || area.y + area.h <= s.y0) return;
    const bool hi = alertLevel(a) == 2; const Beat b = beatFor(hi); const int age = a.age;
    const uint16_t edge = a.conf == ui::CONF_HIGH ? ui::safeEdge(pal::RED, ui::SAFE_HIGH, pal::VOID, pal::PANEL)
                        : a.conf == ui::CONF_MED  ? ui::safeEdge(pal::AMBER, ui::SAFE_MED, pal::VOID, pal::PANEL)
                                                  : ui::safeEdge(pal::WITCH, ui::SAFE_LOW, pal::VOID, pal::PANEL);
    Rc r{ s, area.x, area.y, area.w, area.h };
    const AlertGeom gm = alertGeom(area, hi); const int mx = gm.mx, my = gm.my, R = gm.R;
    const bool strobe = hi && age < 360 && ((age / 60) & 1) == 0;
    afill(r, area.x, area.y, area.w, area.h, strobe ? ui::Canvas::mix(pal::VOID, edge, 1, 4) : pal::VOID);
    switch (riteFor(sc.theme, a.style)) {
        case RITE_SUMMON: riteSummon(r, sc, edge, age, b, mx, my, R); break;
        case RITE_SUPERNOVA: riteSupernova(r, sc, edge, age, b, mx, my, R); break;
        case RITE_VORTEX: riteVortex(r, sc, edge, age, b, mx, my, R); break;
        case RITE_COMET: riteComet(r, sc, edge, age, b, mx, my, R); break;
        case RITE_LOCKON: riteLockOn(r, sc, edge, age, b, mx, my, R); break;
        default: riteWard(r, sc, edge, age, b, mx, my, R); break;
    }
    if (age >= b.flare) alertText(r, a, edge, age, b);
    if (hi) {                                                // the whole-screen alarm: pulsing border and corner brackets
        aframe(r, 4, edge, 150 + 105 * isin(age * 3) / 1024);
        const int L = 34;
        for (int c = 0; c < 4; c++) { const int x = c & 1 ? area.x + area.w - 10 : area.x + 10, y = c & 2 ? area.y + area.h - 10 : area.y + 10, sx = c & 1 ? -1 : 1, sy = c & 2 ? -1 : 1;
            aline(r, x, y, x + sx * L, y, pal::BRIGHT, 255, 2); aline(r, x, y, x, y + sy * L, pal::BRIGHT, 255, 2); }
    }
}

}  // namespace scanr
