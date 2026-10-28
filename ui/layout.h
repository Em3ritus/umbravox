// Screen geometry, shared.
//
// These live here because two things need them and must agree: the code that
// draws a tile, and the code that decides whether a finger landed on it. A
// private copy in each would drift the first time a row moved.
#pragma once

namespace ui {

constexpr int HDR_H     = 12;            // header; its last row is the border
constexpr int FOOT_Y    = 103;           // footer border row

// UMBRAVOX
constexpr int COL_L_X   = 2,  COL_L_W = 56;
constexpr int COL_R_X   = 61, COL_R_W = 97;
constexpr int TILE_H    = 28;
constexpr int TILE_Y[3] = {13, 43, 73};
// The dial's geometry is part of the theme, not of the layout: the two
// themes want different sizes, and the alternative is a dial that fits one
// of them badly. These are written by pal::setTheme().
//
// They stay in ui:: under the names they always had, so screen_scan.cpp and
// nav.cpp read them unchanged -- which is the point of this file. The code
// that draws the dial and the code that decides whether a finger landed on
// it must agree, and they still cannot disagree.
//
// SPEC_H == 0 means the theme has no spectrum strip.
extern int DIAL_Y, DIAL_H;
extern int DIAL_CX, DIAL_CY;
extern int R_FIELD, R_RUNES, R_BOUND, R_STAR;
extern int SPEC_Y, SPEC_H;

// CODEX
constexpr int BODY_Y    = 13;
constexpr int BODY_H    = FOOT_Y - BODY_Y - 2;     // 88
constexpr int ROW_H     = 12;
constexpr int ROWS      = BODY_H / ROW_H;          // 7

// The log start/stop control: the footer's SD status, rune and name. It stops
// short of the clock at the right so the two never share a tap.
constexpr int LOG_X = 86, LOG_W = 70;   // record button, right of the GPS status
constexpr int LOG_Y = FOOT_Y + 1, LOG_H = 120 - FOOT_Y - 1;
// The scan footer's mesh node count, in the gap between the GPS label (which
// ends at x 46 for "NO GPS") and the record button: a glyph or "N", then up to
// two digits. 24 px holds "N99" (17 px) with room either side.
constexpr int NODE_X = 56, NODE_W = 24;

// The back control on CODEX and ATTUNE: the header's left end.
constexpr int BACK_W    = 24;
// The way in to ATTUNE: a mark in the header's spare middle.
constexpr int ATTUNE_X  = 98, ATTUNE_W = 20;
// ...and the record of what has fired, beside it.
constexpr int OMENS_X   = 74, OMENS_W  = 22;

// DOMAIN: the territory map
constexpr int MAP_CX = 79, MAP_CY = 57, MAP_R = 42;
inline constexpr uint16_t RANGES[] = {100, 250, 500, 1000, 2500};
constexpr int RANGE_N = (int)(sizeof(RANGES) / sizeof(RANGES[0]));

// GRIMOIRE: the tile grid. Three columns of 50 fit 158 with 4px gutters.
constexpr int GRIM_COLS = 3;
constexpr int GRIM_X = 3, GRIM_Y = 15;
constexpr int GRIM_W = 50, GRIM_H = 26, GRIM_GAP = 4;

// ATTUNE rows
// Seven rows at 12px start at 14 and end at 98, inside a body that ends at
// 100. Eight would not fit, which is the ceiling on this screen.
constexpr int SET_Y     = 14;
constexpr int SET_H     = 12;
// The IDLE row's -/+ step one list: NEVER, then each timeout with DIM, OFF
// and ANIM (see idleStep). An eighth row would not fit, so the mode shares it.
constexpr int SET_MINUS_X = 114, SET_PLUS_X = 136, SET_BTN_W = 18, SET_BTN_H = 10;
// The pager: "PAGE 1/2" in the RESET row, between the label (ends x 34) and
// CLEAR (starts at SET_MINUS_X). It stops 18 px short of CLEAR so a finger
// aimed at the page cannot land on the destructive button. Tapping it flips
// round-robin through SET_PAGES, like the IDLE row's list.
constexpr int SET_PAGE_X = 40, SET_PAGE_W = 56;

// MESH screen network-control row: the net id, then ENROLL and RESET buttons.
constexpr int MESH_NET_Y = 85;
constexpr int MESH_ENR_X = 62, MESH_ENR_W = 54;
constexpr int MESH_RST_X = 120, MESH_RST_W = 37;
constexpr int MESH_BTN_H = 11;

// ASPECT: a live dial over two pickers. The preview is big enough to judge a
// sweep by; the rows reuse ATTUNE's button geometry so the controls are the
// same object in both places.
constexpr int ASP_CY     = 38;      // preview centre
constexpr int ASP_R      = 24;      // preview outer bound
constexpr int ASP_ROW_Y  = 65;      // first picker row
constexpr int ASP_ROW_H  = 12;
constexpr int ASP_ROWS   = 3;       // THEME, SWEEP, ALERT
// The THEME row's value fits this many characters between the label and the
// buttons; a longer pack name is cut to it.
constexpr int ASP_NAME_MAX = 12;
// "N SKIPPED" / "DIAG LOG", shown when theme packs were skipped at boot. The
// full sentence (24 characters, 143 px) does not fit beside a 48 px preview,
// and across the top it would cover the preview, so it is split into the two
// top corners of the body, one line high, either side of the disc. Each cell
// takes up to 10 characters at 6 px; the test holds both clear of the disc.
constexpr int ASP_SKIP_Y  = 15, ASP_SKIP_H = 7;
constexpr int ASP_SKIP_LX = 3,  ASP_SKIP_RX = 98, ASP_SKIP_W = 59;

// FILES: a count line, five log rows, and an action bar, inside the body.
constexpr int FILE_INFO_Y = 15;                 // "12 LOGS" / "3.1G USED"
constexpr int FILE_ROW_Y  = 24, FILE_ROW_H = 12; // FILE_VIS rows: 24..83
constexpr int FILE_ROW_X  = 3,  FILE_ROW_W = 150; // the scrollbar is right of it
constexpr int FILE_BAR_Y  = 87, FILE_BAR_H = 12;  // the action bar
constexpr int FILE_DEL_X  = 104, FILE_DEL_W = 52; // DELETE, bottom right
// Row text: the label (cut to FILE_LABEL_MAX characters so it never reaches
// the REC column), REC, then the size right-aligned.
constexpr int FILE_REC_X = FILE_ROW_X + 101;
constexpr int FILE_LABEL_MAX = 16;              // "2026-01-27 15:30"
// The confirmation card covers the rows and the bar. NO is the default: the
// larger target, where the finger that tapped DELETE already is; YES is small
// and far from it, so neither a bounce nor a lazy second tap can say yes.
constexpr int FILE_CARD_X = 4,  FILE_CARD_Y = 24, FILE_CARD_W = 152, FILE_CARD_H = 76;
constexpr int FILE_YES_X  = 10, FILE_YES_Y = 78, FILE_YES_W = 34, FILE_YES_H = 16;
constexpr int FILE_NO_X   = 70, FILE_NO_Y  = 70, FILE_NO_W  = 80, FILE_NO_H  = 26;
// The way in: ATTUNE page 2, the row under AUTO LOG, an OPEN button where
// page 1's -/+ pairs sit. (GRIMOIRE's 3x3 grid is full.)
constexpr int SET_FILES_ROW = 1;                // on page 2 (setPage == 1)

} // namespace ui
