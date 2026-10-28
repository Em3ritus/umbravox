// Portrait geometry: the tall 120x160 canvas (ORIENT_PORTRAIT).
//
// The same rule as ui/layout.h: the code that draws a portrait control and
// the code that decides whether a finger landed on it read these names, so
// the two cannot drift. sim/test_portrait.cpp holds every tappable zone
// inside 120x160, at least 14 px each way, and clear of every other zone on
// the same screen.
//
// The dial's height and its centre's offset from the panel top are the
// theme's (DIAL_H, DIAL_CY - DIAL_Y, and the radii); only where the panel
// sits is portrait's. Both dial sizes the themes use (70 and 90 px tall)
// fit between the tiles and the footer, and the 116-px-wide panel is wider
// than landscape's 97, so a theme's dial -- and its emblem -- is drawn at
// exactly its landscape size.
#pragma once
#include "layout.h"

namespace ui {
namespace pl {

constexpr int PW = 120, PH = 160;

// Header: rows 0..13, the border on row 14. Taller than landscape's 12 so
// the menu/back zone meets the 14-px minimum.
constexpr int HDR_H   = 15;
constexpr int CHIP_W  = 16;              // the drawn menu/back chip
constexpr int BACK_W  = 24;              // its tap zone: 0..23 x 0..HDR_H-1
constexpr int TITLE_X = CHIP_W + 3;
constexpr int BADGE_X = 102, BADGE_W = 17;   // the 24 / 5G badge
// Title characters that fit between the chip and the badge.
constexpr int TITLE_MAX = (BADGE_X - 2 - TITLE_X) / 6;     // 13

// The footer: border row, an info row (GPS, node count), and a full-width
// record button. The tap zone is the button plus a pixel all round.
constexpr int FOOT_Y  = 128;
constexpr int FOOT_TY = FOOT_Y + 3;      // the info row's text
constexpr int LOG_X = 2, LOG_Y = 140, LOG_W = 116, LOG_H = 19;

// The body between header and footer, for every screen but SCAN.
constexpr int BODY_Y = HDR_H, BODY_H = FOOT_Y - BODY_Y;       // 15, 113

// SCAN: three count tiles in a row, the dial panel, the spectrum.
constexpr int TILE_X0 = 2, TILE_STEP = 39, TILE_W = 38;
constexpr int TILE_Y  = 16, TILE_H = 20;
constexpr int DIAL_X  = 2, DIAL_W = 116, DIAL_Y = 37;
inline int dialH()  { return ui::DIAL_H; }
inline int dialCX() { return DIAL_X + DIAL_W / 2; }
inline int dialCY() { return DIAL_Y + (ui::DIAL_CY - ui::DIAL_Y); }
inline int specY()  { return DIAL_Y + (ui::SPEC_Y - ui::DIAL_Y); }   // SPEC_H is the theme's
inline void tileRect(int i, int& x, int& y, int& w, int& h) {
    x = TILE_X0 + i * TILE_STEP; y = TILE_Y; w = TILE_W; h = TILE_H;
}

// GRIMOIRE: two columns of four, then the LABELS toggle across the width.
// Two columns because plain labels run to "SETTINGS" (48 px).
constexpr int GRIM_COLS = 2;
constexpr int GRIM_X = 3, GRIM_Y = 17, GRIM_W = 56, GRIM_H = 20;
constexpr int GRIM_GAP_X = 2, GRIM_GAP_Y = 2;
inline void grimCell(int i, int& x, int& y, int& w, int& h) {
    const int row = i / GRIM_COLS;
    y = GRIM_Y + row * (GRIM_H + GRIM_GAP_Y);
    h = GRIM_H;
    if (i >= 8) { x = GRIM_X; w = GRIM_COLS * GRIM_W + GRIM_GAP_X; return; }   // LABELS
    x = GRIM_X + (i % GRIM_COLS) * (GRIM_W + GRIM_GAP_X);
    w = GRIM_W;
}

// ATTUNE: seven 16-px rows. Label over value on the left, -/+ on the right;
// the last row is the pager and CLEAR, 14 px apart.
constexpr int SET_Y = 16, SET_H = 16;
constexpr int SET_LABEL_X = 4;
constexpr int SET_MINUS_X = 74, SET_PLUS_X = 97, SET_BTN_W = 20, SET_BTN_H = 14;
constexpr int SET_WIDE_X = SET_MINUS_X, SET_WIDE_W = SET_PLUS_X + SET_BTN_W - SET_MINUS_X;  // OPEN, CLEAR
constexpr int SET_PAGE_X = 4, SET_PAGE_W = 56;
constexpr int SET_VALUE_MAX = (SET_MINUS_X - 4 - SET_LABEL_X) / 6;     // 11 characters
inline int setRowY(int r) { return SET_Y + r * SET_H; }

// FILES: the count line, five two-line rows, the action bar; the delete
// card covers rows and bar. NO is the big target over where DELETE was.
constexpr int FILE_INFO_Y = 17;
constexpr int FILE_ROW_X = 3, FILE_ROW_Y = 27, FILE_ROW_W = 110, FILE_ROW_H = 16;
constexpr int FILE_BAR_Y = 109, FILE_BAR_H = 16;
constexpr int FILE_DEL_X = 64, FILE_DEL_W = 52;
constexpr int FILE_CARD_X = 3, FILE_CARD_Y = 27, FILE_CARD_W = 114, FILE_CARD_H = 98;
constexpr int FILE_YES_X = 8,  FILE_YES_Y = 102, FILE_YES_W = 32, FILE_YES_H = 18;
constexpr int FILE_NO_X  = 48, FILE_NO_Y  = 90,  FILE_NO_W  = 66, FILE_NO_H  = 32;

// The placeholder for screens without a portrait layout yet.
constexpr int PH_CARD_X = 6, PH_CARD_Y = 20, PH_CARD_W = 108, PH_CARD_H = 76;
constexpr int PH_BACK_X = 10, PH_BACK_Y = 101, PH_BACK_W = 100, PH_BACK_H = 22;

} // namespace pl
} // namespace ui
