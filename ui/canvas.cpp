#include "canvas.h"
#include "font5x7.h"

namespace ui {

void Canvas::clear(uint16_t c) {
    for (int i = 0; i < W * H; i++) px[i] = c;
}

void Canvas::pixel(int x, int y, uint16_t c) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    px[y * W + x] = c;
}

void Canvas::hline(int x, int y, int w, uint16_t c) {
    if (w < 0) { x += w; w = -w; }
    for (int i = 0; i < w; i++) pixel(x + i, y, c);
}

void Canvas::vline(int x, int y, int h, uint16_t c) {
    if (h < 0) { y += h; h = -h; }
    for (int i = 0; i < h; i++) pixel(x, y + i, c);
}

void Canvas::line(int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    int err = (dx > dy ? dx : -dy) / 2;
    for (;;) {
        pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e = err;
        if (e > -dx) { err -= dy; x0 += sx; }
        if (e <  dy) { err += dx; y0 += sy; }
    }
}

void Canvas::rect(int x, int y, int w, int h, uint16_t c) {
    if (w <= 0 || h <= 0) return;
    hline(x, y, w, c);
    hline(x, y + h - 1, w, c);
    vline(x, y, h, c);
    vline(x + w - 1, y, h, c);
}

void Canvas::fill(int x, int y, int w, int h, uint16_t c) {
    for (int j = 0; j < h; j++) hline(x, y + j, w, c);
}

void Canvas::circle(int cx, int cy, int r, uint16_t c) {
    if (r < 0) return;
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        pixel(cx + x, cy + y, c); pixel(cx + y, cy + x, c);
        pixel(cx - y, cy + x, c); pixel(cx - x, cy + y, c);
        pixel(cx - x, cy - y, c); pixel(cx - y, cy - x, c);
        pixel(cx + y, cy - x, c); pixel(cx + x, cy - y, c);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void Canvas::disc(int cx, int cy, int r, uint16_t c) {
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++)
            if (i * i + j * j <= r * r) pixel(cx + i, cy + j, c);
}

void Canvas::dottedRect(int x, int y, int w, int h, uint16_t c, int on, int off) {
    const int period = on + off;
    if (period <= 0 || w <= 0 || h <= 0) return;
    int t = 0;
    for (int i = 0; i < w; i++, t++) {
        if (t % period < on) { pixel(x + i, y, c); pixel(x + i, y + h - 1, c); }
    }
    t = 0;
    for (int j = 0; j < h; j++, t++) {
        if (t % period < on) { pixel(x, y + j, c); pixel(x + w - 1, y + j, c); }
    }
}

void Canvas::dottedCircle(int cx, int cy, int r, uint16_t c, int step) {
    if (r <= 0 || step <= 0) return;
    // Walk the Bresenham octant and keep every `step`-th point. Spacing comes
    // out even enough for a range ring and costs no trig.
    int x = r, y = 0, err = 1 - r, n = 0;
    while (x >= y) {
        if (n % step == 0) {
            pixel(cx + x, cy + y, c); pixel(cx + y, cy + x, c);
            pixel(cx - y, cy + x, c); pixel(cx - x, cy + y, c);
            pixel(cx - x, cy - y, c); pixel(cx - y, cy - x, c);
            pixel(cx + y, cy - x, c); pixel(cx + x, cy - y, c);
        }
        n++;
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void Canvas::text(int x, int y, const char* s, uint16_t c, int scale) {
    if (!s || scale < 1) return;
    for (int i = 0; s[i]; i++) {
        const int gx = x + i * Font::ADV * scale;
        if (gx >= W) break;
        for (int r = 0; r < Font::H; r++) {
            const uint8_t bits = Font::row(s[i], r);
            if (!bits) continue;
            for (int b = 0; b < Font::W; b++) {
                if (!((bits >> (Font::W - 1 - b)) & 1)) continue;
                if (scale == 1) pixel(gx + b, y + r, c);
                else fill(gx + b * scale, y + r * scale, scale, scale, c);
            }
        }
    }
}

void Canvas::textRight(int xRight, int y, const char* s, uint16_t c, int scale) {
    text(xRight - Font::width(s, scale), y, s, c, scale);
}

void Canvas::textCenter(int xCenter, int y, const char* s, uint16_t c, int scale) {
    text(xCenter - Font::width(s, scale) / 2, y, s, c, scale);
}

uint16_t Canvas::mix(uint16_t a, uint16_t b, int n, int d) {
    if (d <= 0) return a;
    if (n <= 0) return a;
    if (n >= d) return b;
    const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    const int r = ar + (br - ar) * n / d;
    const int g = ag + (bg - ag) * n / d;
    const int bl = ab + (bb - ab) * n / d;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

void Canvas::vignette() {
    // Squared radius, so no sqrt. Full brightness out to ~45% of the corner
    // distance, then a linear fall to 62% at the very corners.
    constexpr int CX = W / 2, CY = H / 2;
    constexpr int MAXD = CX * CX + CY * CY;
    for (int y = 0; y < H; y++) {
        const int dy = y - CY;
        for (int x = 0; x < W; x++) {
            const int dx = x - CX;
            const int t = (dx * dx + dy * dy) * 100 / MAXD;   // 0..100
            if (t <= 45) continue;
            const int f = 100 - (t - 45) * 26 / 55;           // 100..74
            uint16_t p = px[y * W + x];
            const int r = (((p >> 11) & 0x1F) * f) / 100;
            const int g = (((p >>  5) & 0x3F) * f) / 100;
            const int b = (( p        & 0x1F) * f) / 100;
            px[y * W + x] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
}

void Canvas::scanlines() {
    for (int y = 1; y < H; y += 2)
        for (int x = 0; x < W; x++) {
            uint16_t c = px[y * W + x];
            const int r = ((c >> 11) & 0x1F) * 11 / 16;
            const int g = ((c >>  5) & 0x3F) * 11 / 16;
            const int b = ( c        & 0x1F) * 11 / 16;
            px[y * W + x] = (uint16_t)((r << 11) | (g << 5) | b);
        }
}

} // namespace ui
