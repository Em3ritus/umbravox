// WCAG contrast between two RGB565 colours, and the rule that keeps an alert
// edge visible whatever palette a theme pack chose.
//
// Header-only and pure: no palette, no Arduino, so host tests and the device
// share it. The rule is applied where the edge is drawn (drawAlert), never by
// rewriting palette roles -- WITCH is also the chrome colour on every screen.
#pragma once
#include <math.h>
#include <stdint.h>

namespace ui {

constexpr int MIN_EDGE_CONTRAST_X10 = 30;   // 3:1

// Fixed fallbacks, RGB565 of #EF4444 / #F59E0B / #D6C2FF (as pal::rgb).
constexpr uint16_t SAFE_HIGH = (uint16_t)(((0xEF & 0xF8) << 8) | ((0x44 & 0xFC) << 3) | (0x44 >> 3));
constexpr uint16_t SAFE_MED  = (uint16_t)(((0xF5 & 0xF8) << 8) | ((0x9E & 0xFC) << 3) | (0x0B >> 3));
constexpr uint16_t SAFE_LOW  = (uint16_t)(((0xD6 & 0xF8) << 8) | ((0xC2 & 0xFC) << 3) | (0xFF >> 3));

namespace detail {
inline float linChan(int v8) {
    const float c = v8 / 255.0f;
    return c <= 0.03928f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}
inline float luminance(uint16_t c) {
    const int r5 = c >> 11, g6 = (c >> 5) & 63, b5 = c & 31;
    const int r = (r5 << 3) | (r5 >> 2), g = (g6 << 2) | (g6 >> 4), b = (b5 << 3) | (b5 >> 2);
    return 0.2126f * linChan(r) + 0.7152f * linChan(g) + 0.0722f * linChan(b);
}
}  // namespace detail

// WCAG contrast ratio x10, truncated (never rounded up past the threshold).
// 210 = black on white, 10 = identical colours.
inline int contrastX10(uint16_t a565, uint16_t b565) {
    float la = detail::luminance(a565), lb = detail::luminance(b565);
    if (la < lb) { const float t = la; la = lb; lb = t; }
    return (int)(((la + 0.05f) / (lb + 0.05f)) * 10.0f + 0.0001f);
}

// `col` if it reads against BOTH backgrounds, else the fixed `safe` colour.
inline uint16_t safeEdge(uint16_t col, uint16_t safe, uint16_t bg1, uint16_t bg2) {
    return (contrastX10(col, bg1) >= MIN_EDGE_CONTRAST_X10 &&
            contrastX10(col, bg2) >= MIN_EDGE_CONTRAST_X10) ? col : safe;
}

}  // namespace ui
