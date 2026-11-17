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
constexpr size_t MAX_JSON = 4096;

enum Board : uint8_t { B_WAVESHARE, B_CYD, B_N };   // 480x320, 320x240

enum Piece : uint8_t {
    P_MENU, P_WORDMARK, P_GPS, P_NODES, P_POWER, P_BRIDGE, P_SD,
    P_NAV_SCAN, P_NAV_DOMAIN, P_NAV_CODEX, P_NAV_POI, P_STOP,
    P_COUNT_WIFI, P_COUNT_5G, P_COUNT_BLE, P_N
};
extern const char* const PIECE_NAME[P_N];

enum How : uint8_t { DEFAULT = 0, PLACED, HIDDEN };
struct Slot { How how; int16_t x, y, w, h; };          // x,y,w,h meaningful only when PLACED (w,h > 0)
struct Layout { bool has[B_N]; Slot slot[B_N][P_N]; };  // zero-initialised = no boards, all DEFAULT

// Parse n bytes of layout.json. On failure returns false and writes a short
// reason into why (if whyCap > 0). `out` is only meaningful on true.
bool parse(const char* json, size_t n, Layout& out, char* why, size_t whyCap);

Board boardFor(int W);                         // W >= 400 -> B_WAVESHARE, else B_CYD
uieng::Rect defaultRect(Board b, Piece p);     // the built-in rect of a piece on a board

// Fill every piece of board b (DEFAULT -> its built-in rect, HIDDEN -> {0,0,0,0}),
// place the body and shift Scan's rects, and check the result. Returns false with a
// reason when any rule fails: a piece off screen, a tappable piece under 44 px, a
// piece that cannot be hidden, two pieces overlapping, a tile over the radar, or too
// little room left for the body. The only way to make a ChromeOverride.
bool resolve(const Layout& L, Board b, uieng::ChromeOverride& out, char* why, size_t whyCap);

}  // namespace themelayout
