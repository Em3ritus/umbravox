// The designer's speed meter: replay a theme's SCAN screen (or an alert on it) for a number of frames the way the
// board's engine does (snapshot -> commit -> slices of the shared loop in ui/scan_frame.h -> present), and count the
// pixels drawn and pushed in each frame. The designer turns the counts into milliseconds with a fit measured on the
// boards (tools/meterfit.py -> web/designer/js/meter-fit.json).
//
// Host and wasm only: never compiled into the firmware (it allocates two frames on the heap per run).
#pragma once
#include "preview_ws.h"
#include "frame_present.h"
#include "scan_frame.h"
#include <memory>
#include <vector>

namespace meter {

// Scan frames drawn before an alert is raised: on the board an alert rises over a drawn scan screen, so the replay's
// alert frame 0 must not be the entry repaint of the whole screen. Their counts are not returned.
constexpr int ALERT_WARMUP = 2;

// mode: -1 the scan screen, 0/1/2 an alert of LOW/MED/HIGH confidence (the theme's own rite), raised after
// ALERT_WARMUP scan frames. frameMs: the board's frame period (the demo clock and the alert's age advance by it).
// Writes `frames` counts to out (scan frames, or the alert's frames from the one it rises in) and returns how many
// (0 for a bad argument).
inline int run(int theme, int w, int h, int mode, int frames, int frameMs, uimeter::Count* out) {
    if (!out || frames < 1 || frameMs < 1 || w < 1 || h < 1 || w > prevws::W || h > prevws::H || mode < -1 || mode > 2) return 0;
    pal::setTheme((uint8_t)theme);
    std::vector<uint16_t> front(w * h), back(w * h), buf(w * 16);
    framepresent::Frame f; f.begin(front.data(), back.data(), w, h);
    std::unique_ptr<scanpres::Engine> e(new scanpres::Engine());
    e->reset(w, h); e->enter();
    struct Stage {
        framepresent::Frame& f;
        void full(int sy, const uint16_t* b, int sh) { f.stage(sy, b, sh); }
        void window(int sy, const uint16_t* b, int sh, int lo, int bw) { f.stageWindow(sy, b, sh, lo, bw); }
        void region(int sy, const uint16_t* b, int sh, int lo, int bw, int id) { f.stageRegion(sy, b, sh, lo, bw, id); }
    } stage{f};
    const int warm = mode >= 0 ? ALERT_WARMUP : 0;
    const ui::State first = ui::demoState(0);
    for (int k = 0; k < warm + frames; k++) {
        const bool alertOn = k >= warm && mode >= 0;
        ui::State s; uiproj::Chrome c;
        prevws::demo(theme, k, -1, alertOn, -1, alertOn ? mode : 0, s, c, frameMs);
        if (alertOn) s.alertAge = (uint16_t)((k - warm) * frameMs);   // the alert's age counts from when it rose
        // The demo moves its beam 5 degrees per frame and recomputes every blip's age from it, so every blip changes
        // every frame and the whole dial is redrawn. On the board a blip keeps the time it was last heard and fades
        // with the clock: keep frame 0's blips and age them by the frame period (ui/project.h: 240 ms per age step).
        for (int i = 0; i < s.nContacts && i < first.nContacts; i++) {
            const int a = first.contacts[i].age + k * frameMs / 240;
            s.contacts[i] = first.contacts[i];
            s.contacts[i].age = (uint8_t)(a > 255 ? 255 : a);
        }
        scanpres::SceneStore& st = e->back();
        uiproj::scan(st, s, c, alertOn); st.animate(true); st.fx(true); st.sweep(s.set.sweep);
        e->commit();
        uimeter::Count n;
        while (e->pending()) {
            const auto sl = e->nextSlice(4);                 // the board's budget per loop (device/main.cpp)
            if (sl.y1 > sl.y0) scanframe::slice(*e, e->scene(), w, h, sl.y0, sl.y1, buf.data(), f.valid(), stage, n);
            if (sl.done) n.pushed = f.present(false, [](uieng::Rect, const uint16_t*, int) {}, 0, h);
        }
        if (k >= warm) out[k - warm] = n;
    }
    return frames;
}

} // namespace meter
