// Theme layout: the parsed form of a pack's optional /themes/<folder>/layout.json.
//
// A layout moves, resizes or hides the shared header and footer pieces and Scan's
// count tiles, per engine board. Like theme.json it is untrusted data: parse() is
// bounded, allocation-free and never assumes the input is NUL terminated; on any
// defect it returns false with a short reason (check the return value, not the
// struct). See docs/theme-packs.md "layout.json".
//
// Free of Arduino and palette.h so it links into the host sim, the tests and the
// device alike.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "layout_engine.h"

namespace themelayout {

constexpr int FORMAT = 1;
constexpr size_t MAX_JSON = 16384;   // limits.json MAX_LAYOUT_JSON
constexpr int MAX_FONT_REFS = 8, MAX_ART_REFS = 16;   // f0..f7.fnt, a0..a15.art

enum Board : uint8_t { B_WAVESHARE, B_CYD, B_N };   // 480x320, 320x240
// Pieces that may carry an alert effect on a board (each costs a repaint every alert frame).
constexpr int fxCap(Board b) { return b == B_WAVESHARE ? 4 : 2; }

enum Piece : uint8_t {
    P_MENU, P_WORDMARK, P_GPS, P_NODES, P_POWER, P_BRIDGE, P_SD,
    P_NAV_SCAN, P_NAV_DOMAIN, P_NAV_CODEX, P_NAV_POI, P_STOP,
    P_COUNT_WIFI, P_COUNT_5G, P_COUNT_BLE,
    P_RADAR, P_SPECTRUM, P_CHANNEL, P_BADGE, P_N
};
extern const char* const PIECE_NAME[P_N];
static_assert(P_N == uieng::PIECE_SLOTS, "layout_engine.h PIECE_SLOTS must match the piece list");

enum How : uint8_t { SLOT_DEFAULT = 0, SLOT_PLACED, SLOT_HIDDEN };
struct Slot { How how; int16_t x, y, w, h; };          // x,y,w,h meaningful only when SLOT_PLACED (w,h > 0)
struct Layout { bool has[B_N]; Slot slot[B_N][P_N]; };  // zero-initialised = no boards, all SLOT_DEFAULT

// Parse n bytes of layout.json. On failure returns false and writes a short
// reason into why (if whyCap > 0). `out` is only meaningful on true.
bool parse(const char* json, size_t n, Layout& out, char* why, size_t whyCap);
// The same, also filling looks[B_N] (piece styles, style block, decor). looks may be null: styles are then
// checked and dropped.
bool parse(const char* json, size_t n, Layout& out, uieng::BoardLook* looks, char* why, size_t whyCap);

Board boardFor(int W);                         // W >= 400 -> B_WAVESHARE, else B_CYD
uieng::Rect defaultRect(Board b, Piece p);     // the built-in rect of a piece on a board

// Fill every piece of board b (SLOT_DEFAULT -> its built-in rect, SLOT_HIDDEN -> {0,0,0,0}),
// place the body and shift Scan's rects, and check the result. Returns false with a
// reason when any rule fails: a piece off screen, a tappable piece under 44 px, a
// piece that cannot be hidden, two pieces overlapping, a tile over the radar, or too
// little room left for the body. The only way to make a ChromeOverride.
bool resolve(const Layout& L, Board b, uieng::ChromeOverride& out, char* why, size_t whyCap);

// A pack's resolved layout, as pal keeps it. lo.board[b] points into ov[b] (or is null
// for a board the file leaves out), so a PackLayout must stay where build() filled it.
struct PackLayout { uieng::LayoutOverride lo; uieng::ChromeOverride ov[B_N]; uieng::BoardLook look[B_N]; };

// The checks a look needs its board and the theme's colours for: decor on screen, and text against fill at
// least 3:1 on menu, stop, nav.* and bridge. colors (themepack::N_COLORS, RGB565) may be null: role colours
// are then not checked.
bool checkLook(const uieng::BoardLook& k, Board b, const uint16_t* colors, char* why, size_t whyCap);

// parse + resolve every board the file gives. False with "layout.json: <reason>" (or
// "layout.json over 16384 bytes") in why; out.lo is then all null.
bool build(const char* json, size_t n, PackLayout& out, char* why, size_t whyCap, const uint16_t* colors = nullptr);

}  // namespace themelayout
