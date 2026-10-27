#include "nav.h"
#include "layout.h"
#include "signatures.h"
#include "screens.h"
#include "palette.h"

namespace ui {
namespace {

// A press that moves less than this is a tap, not a drag.
constexpr int TAP_SLOP   = 6;
// ...and a drag does not begin until the finger has clearly committed, so a
// slightly unsteady tap does not scroll the list out from under itself.
constexpr int DRAG_START = 5;

// Finger pixels per row of scroll.
//
// The first version subtracted raw pixels from a row index, so one pixel of
// finger moved the list a whole row -- which is what "too fast and touchy"
// was. One row per row-height is 1:1 with the content.
constexpr int ROW_PX = ROW_H;

int iabs(int v) { return v < 0 ? -v : v; }

bool hit(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

// Step a setting, clamped. Steps are coarse on purpose -- these are knobs on
// a 2.8" panel being poked with a finger, not a spin box.
void adjust(Settings& st, int row, int dir) {
    switch (row) {
        case 0: {
            int v = (int)st.alertMin + dir;
            st.alertMin = (uint8_t)(v < 0 ? 0 : v > CONF_OFF ? CONF_OFF : v);
            break;
        }
        case 5: {
            int v = (int)st.idleIdx + dir;
            st.idleIdx = (uint8_t)((v + IDLE_N) % IDLE_N);
            break;
        }
        case 4: {
            // A table rather than a step, because 10% was still comfortably
            // readable -- the useful range keeps going down, and it needs
            // finer stops at the bottom where a 10-point step is everything.
            static const uint8_t L[] = {1, 3, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
            constexpr int N = (int)(sizeof(L) / sizeof(L[0]));
            int i = 0;
            while (i < N - 1 && L[i] < st.bright) i++;
            i += dir;
            if (i < 0) i = 0;
            if (i > N - 1) i = N - 1;
            st.bright = L[i];
            break;
        }
        case 1: {
            int v = (int)st.dwellMs + dir * 40;
            st.dwellMs = (uint16_t)(v < 100 ? 100 : v > 600 ? 600 : v);
            break;
        }
        case 2: {
            int v = st.bleDuty + dir * 10;
            st.bleDuty = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v);
            break;
        }
        case 3: {
            int v = (int)st.bands + dir;
            st.bands = (uint8_t)((v + 3) % 3);
            break;
        }
        default: break;
    }
}

// Step a theme or a sweep. The sweep steps to the next AVAILABLE id rather
// than clamping: stepping onto the gaze on UMBRAVOX and being bounced back
// reads as a broken control, not as an unavailable option.
void adjustAspect(Settings& st, int row, int dir) {
    if (row == 2) {
        // Alerts are per-theme: only COSMIC MUFFIN has a choice of them.
        if (st.theme != pal::TH_COSMICMUFFIN) return;
        const int v = (int)st.alertStyle + dir;
        st.alertStyle = (uint8_t)((v + ALERT_STYLE_COUNT) % ALERT_STYLE_COUNT);
        return;
    }
    if (row == 0) {
        const int v = (int)st.theme + dir;
        const int n = pal::themeCount();        // built-ins, then packs
        st.theme = (uint8_t)((v + n) % n);
        clampSettings(st);          // the new theme may not offer this sweep
        return;
    }
    const uint16_t mask = pal::sweepMask(st.theme);
    int v = st.sweep;
    for (int tries = 0; tries < SW_COUNT; tries++) {
        v = (v + dir + SW_COUNT) % SW_COUNT;
        if (mask & (uint16_t)(1u << v)) { st.sweep = (uint8_t)v; return; }
    }
}

// Returns true if the tap changed the screen.
bool onTap(Nav& n, Settings& st, int x, int y) {
    if (n.screen == SCR_SCAN) {
        // The three tiles are the navigation. Each opens the codex filtered
        // to whatever that tile counts, which is the obvious meaning of
        // tapping a number: "show me these".
        for (int i = 0; i < 3; i++) {
            if (!hit(x, y, COL_L_X, TILE_Y[i], COL_L_W, TILE_H)) continue;
            n.screen = SCR_CODEX;
            n.filter = (i == 0) ? F_WIFI : (i == 1) ? F_5G : F_BLE;
            n.scroll = 0;
            n.dragAccum = 0;
            return true;
        }
        // The circle opens the territory map: the dial is what is around
        // you now, the map is where you have been.
        if (hit(x, y, COL_R_X, DIAL_Y, COL_R_W, DIAL_H)) {
            n.screen = SCR_DOMAIN;
            return true;
        }
        // The spectrum opens the mesh. It is the obvious target: the
        // spectrum is about channels, and the mesh screen is about who is
        // covering which. A theme without a strip has nothing there, and a
        // tap into empty panel must not navigate.
        if (SPEC_H > 0 && hit(x, y, COL_R_X, SPEC_Y, COL_R_W, SPEC_H)) {
            n.screen = SCR_MESH;
            return true;
        }
        // The footer's SD status is the log start/stop control. nav only
        // reports the tap; whoever owns the card decides what it means.
        if (hit(x, y, LOG_X, LOG_Y, LOG_W, LOG_H)) {
            n.logToggleRequested = true;
            return false;
        }
        // The mark top-left opens the grimoire. It is the one control whose
        // meaning is constant: forward from here, back from anywhere else.
        if (hit(x, y, 0, 0, BACK_W, HDR_H)) {
            n.screen = SCR_GRIMOIRE;
            return true;
        }
        // OMENS (alerts) and ATTUNE (settings) are reached from the menu now,
        // not cryptic header marks -- the menu button opens a labelled grid.
        return false;
    }

    // The back control is the left end of the header everywhere. From the
    // detail screen it returns to the codex that opened it, not all the way
    // out -- going back should undo one step, not the whole journey.
    if (hit(x, y, 0, 0, BACK_W, HDR_H)) {
        if (n.screen == SCR_DETAIL) {
            n.screen = SCR_CODEX;
        } else {
            n.screen = SCR_SCAN;
            n.scroll = 0;
        }
        n.dragAccum = 0;
        return true;
    }

    // A codex row opens that device.
    if (n.screen == SCR_CODEX && y >= BODY_Y && y < BODY_Y + BODY_H) {
        const int row = (y - BODY_Y - 1) / ROW_H;
        if (row >= 0 && row < ROWS) {
            n.selected = (uint16_t)(n.scroll + row);
            n.screen = SCR_DETAIL;
            return true;
        }
    }

    // A grimoire tile opens its screen; the 9th cell toggles the label mode.
    if (n.screen == SCR_GRIMOIRE) {
        int lx, ly, lw, lh;
        grimoireCell(GRIMOIRE_N, lx, ly, lw, lh);
        if (hit(x, y, lx, ly, lw, lh)) {
            st.labels = st.labels ? 0 : 1;
            return true;        // not a screen change, but the grid relabels
        }
        for (int i = 0; i < GRIMOIRE_N; i++) {
            int gx, gy, gw, gh;
            grimoireCell(i, gx, gy, gw, gh);
            if (!hit(x, y, gx, gy, gw, gh)) continue;
            n.screen = GRIMOIRE[i].screen;
            n.scroll = 0;
            n.dragAccum = 0;
            return true;
        }
        return false;
    }

    // A tap anywhere on the omens list does nothing: it is a record, not a
    // menu, and a stray tap should not navigate out of it.
    if (n.screen == SCR_OMENS || n.screen == SCR_ECHOES) return false;

    // Tapping the map cycles its scale. Nothing else on that screen is a
    // control, so the whole body is the button.
    if (n.screen == SCR_DOMAIN && y >= BODY_Y && y < BODY_Y + BODY_H) {
        n.rangeIdx = (uint8_t)((n.rangeIdx + 1) % RANGE_N);
        return false;        // same screen, so no full repaint needed
    }

    if (n.screen == SCR_ATTUNE) {
        for (int r = 0; r < SET_ROWS; r++) {
            const int y0 = SET_Y + r * SET_H;
            if (r == SET_ROWS - 1) {
                const int bw = SET_PLUS_X + SET_BTN_W - SET_MINUS_X;
                if (hit(x, y, SET_MINUS_X, y0 + 1, bw, SET_BTN_H))
                    n.resetRequested = true;
                continue;
            }
            if (hit(x, y, SET_MINUS_X, y0 + 1, SET_BTN_W, SET_BTN_H)) adjust(st, r, -1);
            if (hit(x, y, SET_PLUS_X,  y0 + 1, SET_BTN_W, SET_BTN_H)) adjust(st, r, +1);
        }
    }

    if (n.screen == SCR_ASPECT) {
        for (int r = 0; r < ASP_ROWS; r++) {
            const int y0 = ASP_ROW_Y + r * ASP_ROW_H;
            if (hit(x, y, SET_MINUS_X, y0 + 1, SET_BTN_W, SET_BTN_H)) adjustAspect(st, r, -1);
            if (hit(x, y, SET_PLUS_X,  y0 + 1, SET_BTN_W, SET_BTN_H)) adjustAspect(st, r, +1);
        }
    }

    if (n.screen == SCR_MESH) {
        // The caller owns the radio, so nav only flags the request and clears it.
        if (hit(x, y, MESH_ENR_X, MESH_NET_Y, MESH_ENR_W, MESH_BTN_H)) n.enrollRequested = true;
        if (hit(x, y, MESH_RST_X, MESH_NET_Y, MESH_RST_W, MESH_BTN_H)) n.resetNetRequested = true;
    }
    return false;
}

} // namespace

void clampSettings(Settings& s) {
    if (s.alertMin > CONF_OFF) s.alertMin = CONF_MED;
    if (s.bright < 1)   s.bright = 1;
    if (s.bright > 100) s.bright = 100;
    if (s.dwellMs < 100) s.dwellMs = 100;
    if (s.dwellMs > 600) s.dwellMs = 600;
    if (s.bleDuty > 100) s.bleDuty = 100;
    if (s.bands > 2)     s.bands = 0;
    if (s.idleIdx >= IDLE_N) s.idleIdx = 2;
    if (s.theme >= pal::themeCount()) s.theme = pal::TH_UMBRAVOX;
    if (s.labels > 1)             s.labels = 0;
    if (s.sweep >= SW_COUNT)      s.sweep = SW_RADAR;
    // A sweep the active theme does not offer is not a preference to be
    // preserved; it is a sweep that would draw nothing meaningful. It falls
    // back to the theme's own start (RADAR for the built-ins), because a pack
    // need not offer RADAR.
    if (!(pal::sweepMask(s.theme) & (1 << s.sweep))) s.sweep = pal::startSweep(s.theme);
    if (s.alertStyle >= ALERT_STYLE_COUNT) s.alertStyle = AL_SUPERNOVA;
    // `reserved` is deliberately not clamped: it is compared, never read.
}

bool settingsEqual(const Settings& a, const Settings& b) {
    return a.alertMin == b.alertMin && a.bright  == b.bright  &&
           a.dwellMs  == b.dwellMs  && a.bleDuty == b.bleDuty &&
           a.bands    == b.bands    && a.idleIdx == b.idleIdx &&
           a.theme    == b.theme    && a.sweep   == b.sweep   &&
           a.labels   == b.labels   && a.alertStyle == b.alertStyle &&
           a.reserved == b.reserved;
}

bool navTouch(Nav& n, Settings& st, bool down, int16_t x, int16_t y) {
    if (down) {
        if (!n.tracking) {
            n.tracking  = true;
            n.dragging  = false;
            n.startX    = n.lastX = x;
            n.startY    = n.lastY = y;
            n.dragAccum = 0;
            return false;
        }

        const bool scrollable = (n.screen == SCR_CODEX || n.screen == SCR_OMENS ||
                                 n.screen == SCR_ECHOES);
        if (!n.dragging && scrollable && n.maxScroll > 0 &&
            iabs(y - n.startY) > DRAG_START) {
            n.dragging = true;
        }

        if (n.dragging) {
            // Dragging down moves further down the list -- the finger pushes
            // the viewport, not the content.
            n.dragAccum += (y - n.lastY);
            while (n.dragAccum >= ROW_PX)  { n.scroll++; n.dragAccum -= ROW_PX; }
            while (n.dragAccum <= -ROW_PX) { n.scroll--; n.dragAccum += ROW_PX; }
            if (n.scroll < 0)            { n.scroll = 0;            n.dragAccum = 0; }
            if (n.scroll > n.maxScroll)  { n.scroll = n.maxScroll;  n.dragAccum = 0; }
        }

        n.lastX = x;
        n.lastY = y;
        return false;
    }

    if (!n.tracking) return false;
    n.tracking = false;
    if (n.dragging) { n.dragging = false; return false; }

    // The release carries no position -- the panel reports nothing once the
    // finger is off -- so the tap is measured from the last point seen while
    // it was still down.
    if (iabs(n.lastX - n.startX) > TAP_SLOP ||
        iabs(n.lastY - n.startY) > TAP_SLOP) return false;

    return onTap(n, st, n.lastX, n.lastY);
}

} // namespace ui
