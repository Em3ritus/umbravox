// Touch coordinates across display rotations (pure; sim/test_touchmap.cpp).
//
// The NM-CYD-C5's XPT2046 was calibrated by tapping corners in the landscape
// frame (Arduino_GFX rotation 3), and the touch layer does not turn with the
// display. Rather than calibrate again for portrait, a calibrated point is
// carried to the panel's native 240x320 frame and out again into whatever
// rotation the display is in now -- the glass under the finger is the same.
//
// The four rotations are Arduino_GFX's ST7789 MADCTL settings:
//   0: none          native = (x, y)
//   1: MX | MV       native = (NW-1-y, x)
//   2: MX | MY       native = (NW-1-x, NH-1-y)
//   3: MY | MV       native = (y, NH-1-x)
#pragma once

namespace touchmap {

constexpr int NW = 240, NH = 320;   // native panel, portrait

inline void toNative(int r, int x, int y, int& nx, int& ny) {
    switch (r & 3) {
        case 1:  nx = NW - 1 - y; ny = x;          break;
        case 2:  nx = NW - 1 - x; ny = NH - 1 - y; break;
        case 3:  nx = y;          ny = NH - 1 - x; break;
        default: nx = x;          ny = y;          break;
    }
}

inline void fromNative(int r, int nx, int ny, int& x, int& y) {
    switch (r & 3) {
        case 1:  x = ny;          y = NW - 1 - nx; break;
        case 2:  x = NW - 1 - nx; y = NH - 1 - ny; break;
        case 3:  x = NH - 1 - ny; y = nx;          break;
        default: x = nx;          y = ny;          break;
    }
}

// A panel point measured in rotation `calRot` -> 2x canvas coordinates for a
// display now in rotation `r`.
inline void toCanvas(int calRot, int r, int px, int py, int& cx, int& cy) {
    int nx, ny, x, y;
    toNative(calRot, px, py, nx, ny);
    fromNative(r, nx, ny, x, y);
    cx = x / 2;
    cy = y / 2;
}

} // namespace touchmap
