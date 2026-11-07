// scan_adapter.cpp -- device glue for the SCAN engine. See scan_adapter.h.
//
// Compiled always (device/*.cpp), but its body exists only under the opt-in so
// the default WS build and the NM path carry none of it.
#include "scan_adapter.h"

#if (defined(WS_C5_35) || defined(WS_C5_28)) && defined(UMBRAVOX_SCAN_ENGINE)

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <stdio.h>
#include "../ui/layout_engine.h"    // uieng::scanHits, Action

namespace scanadapter {
namespace {

Arduino_TFT*      gfx_ = nullptr;
int               W_ = 0, H_ = 0;
scanr::Compositor  comp_;
scanpres::Engine   eng_;
scanpres::TouchGate gate_;

// How long the current slice spent pushing pixels (the compositor composes into
// its buffer then calls the sink; timing the sink separates transfer from
// compose).
uint32_t sliceTransferUs_ = 0;

struct PanelSink : scanr::Sink {
    void strip(int x, int y, int w, int h, const uint16_t* px) override {
        const uint32_t t = micros();
        gfx_->startWrite();
        gfx_->writeAddrWindow((int16_t)x, (int16_t)y, (uint16_t)w, (uint16_t)h);
        gfx_->writePixels(const_cast<uint16_t*>(px), (uint32_t)w * h);
        gfx_->endWrite();
        sliceTransferUs_ += micros() - t;
    }
};
PanelSink sink_;

uint8_t navScreen(int screenId) {   // uieng::ScreenId -> ui::Screen
    switch ((uieng::ScreenId)screenId) {
        case uieng::ScreenId::Domain: return ui::SCR_DOMAIN;
        case uieng::ScreenId::Codex:  return ui::SCR_CODEX;
        case uieng::ScreenId::Menu:   return ui::SCR_GRIMOIRE;
        default:                      return ui::SCR_SCAN;
    }
}

}  // namespace

void begin(Arduino_TFT* gfx, int W, int H) {
    gfx_ = gfx; W_ = W; H_ = H;
    eng_.reset(W, H);
}

void enter()        { eng_.enter(); gate_.reset(); }   // swallow a finger held from entry
void wake()         { eng_.wake();  gate_.reset(); }
void themeChanged() { eng_.themeChanged(); }
void leave() {
    eng_.leave();
    gate_.reset();                              // swallow a finger held across the exit
    if (gfx_) gfx_->fillScreen(RGB565_BLACK);   // clear the native paint so the legacy path is clean
}

bool acceptsTouch() { return eng_.acceptsTouch(); }
bool pending()      { return eng_.pending(); }

void snapshot(const ui::State& s, const scanr::GnssFix& gps, scanr::Rec rec,
              bool hasCell, int batt, uint32_t now) {
    scanpres::SceneStore& st = eng_.back();
    st.begin(s.set.theme, now);
    st.gps(gps);
    st.status(s.nNodes, hasCell, batt, rec);
    st.counts(s.nets24, s.nets5, s.ble);

    char ch[12];
    snprintf(ch, sizeof ch, "CH %02u", (unsigned)s.chan);
    st.channel(ch, s.on5 ? "5 GHz" : "2.4 GHz");

    // Fixed 21-channel plan: 13 x 2.4GHz then 8 x 5GHz, straight from State's
    // per-channel activity (kCh24 == CH24, kChannels == CH24 + CH5).
    for (int i = 0; i < ui::CH24; i++) st.setChannel(i, s.load24[i]);
    for (int i = 0; i < ui::CH5;  i++) st.setChannel(ui::CH24 + i, s.load5[i]);

    // The dial field is already polar in State (angle is the MAC hash, stable
    // for the drive; dist is rssi-derived; age is elapsed/240). Carry that real
    // geometry through as a blip -- no invented identity, no re-hash.
    for (int i = 0; i < s.nContacts; i++) {
        const ui::Contact& c = s.contacts[i];
        const uint32_t lastSeen = now - (uint32_t)c.age * 240u;
        st.addBlip(c.angle, c.dist, c.band, lastSeen);
    }
    eng_.commit();
}

bool service(int budgetStrips) {
    if (!eng_.pending()) return false;
    const uint32_t t0 = micros();
    sliceTransferUs_ = 0;
    const scanpres::StripScheduler::Slice sl = eng_.nextSlice(budgetStrips);
    if (sl.y1 > sl.y0)
        comp_.renderRows(W_, H_, eng_.scene(), sl.y0, sl.y1, sink_);
    const uint32_t total = micros() - t0;
    const uint32_t compose = total > sliceTransferUs_ ? total - sliceTransferUs_ : 0;
    eng_.recordSlice(compose, sliceTransferUs_);
    return sl.done;
}

bool dispatchTouch(ui::Nav& nav, bool contact, int panelX, int panelY) {
    // The pure gate fires a down-edge once per press, only while touch is
    // accepted, and consumes a press begun during a transition.
    if (!gate_.edge(contact, eng_.acceptsTouch())) return false;

    uieng::HitCtx ctx;                                   // default: no matched device, no rows
    const uieng::HitList h = uieng::scanHits(W_, H_, ctx);
    for (int i = 0; i < h.count; i++) {
        const uieng::Rect r = h.items[i].rect;
        if (panelX < r.x || panelX >= r.x + r.w || panelY < r.y || panelY >= r.y + r.h) continue;
        switch (h.items[i].action) {
            case uieng::Action::Nav: {
                const uint8_t scr = navScreen(h.items[i].payload);
                if (nav.screen != scr) { nav.screen = scr; return true; }
                return false;
            }
            case uieng::Action::CodexFilter:
                nav.filter = (uint8_t)h.items[i].payload;
                nav.scroll = 0;
                if (nav.screen != ui::SCR_CODEX) { nav.screen = ui::SCR_CODEX; return true; }
                return false;
            case uieng::Action::OpenMesh:
                if (nav.screen != ui::SCR_MESH) { nav.screen = ui::SCR_MESH; return true; }
                return false;
            case uieng::Action::ToggleLogging:
                nav.logToggleRequested = true;           // the caller owns the logger
                return false;
            case uieng::Action::OpenGps:                 // no dedicated GPS screen (as legacy)
            case uieng::Action::PowerDetail:             // WS: not wired as a screen
            default:
                return false;
        }
    }
    return false;
}

void sampleCapture(uint32_t capFrames, uint32_t capUniq, uint32_t bleReports) {
    eng_.sampleCapture(capFrames, capUniq, bleReports);
}
const scanpres::PresentMetrics& metrics() { return eng_.metrics(); }
void clearMetrics() { eng_.clearMetrics(); }

}  // namespace scanadapter

#endif  // WS_C5_35 && UMBRAVOX_SCAN_ENGINE
