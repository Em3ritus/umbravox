// Screen geometry, shared.
//
// These live here because two things need them and must agree: the code that
// draws a tile, and the code that decides whether a finger landed on it. A
// private copy in each would drift the first time a row moved.
#pragma once

namespace ui {

constexpr int HDR_H     = 12;            // header; its last row is the border
constexpr int FOOT_Y    = 103;           // footer border row

// AETHERSCAN
constexpr int COL_L_X   = 2,  COL_L_W = 56;
constexpr int COL_R_X   = 61, COL_R_W = 97;
constexpr int TILE_H    = 28;
constexpr int TILE_Y[3] = {13, 43, 73};
constexpr int DIAL_Y    = 13, DIAL_H = 70;
constexpr int SPEC_Y    = 85, SPEC_H = 16;
constexpr int DIAL_CX   = COL_R_X + COL_R_W / 2;   // 109
constexpr int DIAL_CY   = 48;
constexpr int R_FIELD   = 22;            // inner circle: where contacts live
constexpr int R_RUNES   = 26;            // rune ring centreline
constexpr int R_BOUND   = 31;            // outer circle
constexpr int R_STAR    = 20;            // inscribed heptagram

// CODEX
constexpr int BODY_Y    = 13;
constexpr int BODY_H    = FOOT_Y - BODY_Y - 2;     // 88
constexpr int ROW_H     = 12;
constexpr int ROWS      = BODY_H / ROW_H;          // 7

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
constexpr int GRIM_W = 50, GRIM_H = 27, GRIM_GAP = 4;

// ATTUNE rows
// Seven rows at 12px start at 14 and end at 98, inside a body that ends at
// 100. Eight would not fit, which is the ceiling on this screen.
constexpr int SET_Y     = 14;
constexpr int SET_H     = 12;
constexpr int SET_MINUS_X = 114, SET_PLUS_X = 136, SET_BTN_W = 18, SET_BTN_H = 10;

} // namespace ui
