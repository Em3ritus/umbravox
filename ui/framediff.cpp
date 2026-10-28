#include "framediff.h"
#include <string.h>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace ui {
namespace {

bool    valid = false;
int16_t lastW = 0, lastH = 0;

// One canvas's worth. PSRAM on the board, never internal RAM: the internal
// heap is what Wi-Fi and BLE live on, and this is touched once per frame.
uint16_t* shadow() {
#if defined(ESP_PLATFORM)
    static uint16_t* p = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        p = static_cast<uint16_t*>(heap_caps_malloc(sizeof(uint16_t) * W * H, MALLOC_CAP_SPIRAM));
    }
    return p;
#else
    static uint16_t buf[W * H];
    return buf;
#endif
}

} // namespace

void frameDiffReset() { valid = false; }

bool frameDiffReady() { return shadow() != nullptr; }

void frameDiff(const Canvas& c, Dirty& d) {
    d.clear();
    const int w = c.w, h = c.h;
    uint16_t* sh = shadow();
    if (!sh) { d.add(0, 0, (int16_t)w, (int16_t)h); return; }   // no PSRAM: always everything
    if (!valid || w != lastW || h != lastH) {
        memcpy(sh, c.px, sizeof(uint16_t) * (size_t)(w * h));
        valid = true; lastW = (int16_t)w; lastH = (int16_t)h;
        d.add(0, 0, (int16_t)w, (int16_t)h);
        return;
    }
    for (int y0 = 0; y0 < h; y0 += DIFF_BAND) {
        const int y1 = y0 + DIFF_BAND < h ? y0 + DIFF_BAND : h;
        int minX = w, maxX = -1, minY = -1, maxY = -1;
        for (int y = y0; y < y1; y++) {
            const uint16_t* a = c.px + y * w;
            uint16_t* b = sh + y * w;
            if (memcmp(a, b, sizeof(uint16_t) * (size_t)w) == 0) continue;
            int l = 0, r = w - 1;
            while (a[l] == b[l]) l++;
            while (a[r] == b[r]) r--;
            if (l < minX) minX = l;
            if (r > maxX) maxX = r;
            if (minY < 0) minY = y;
            maxY = y;
            memcpy(b + l, a + l, sizeof(uint16_t) * (size_t)(r - l + 1));
        }
        if (maxX >= 0)
            d.add((int16_t)minX, (int16_t)minY, (int16_t)(maxX - minX + 1), (int16_t)(maxY - minY + 1));
    }
}

} // namespace ui
