// engine_adapter.cpp -- device glue for the whole UI engine. See engine_adapter.h.
//
// Compiled always (device/*.cpp), but its body exists only under the opt-in so
// the default WS build and the NM path carry none of it.
#include "engine_adapter.h"

#if defined(WS_C5_35) && defined(UMBRAVOX_SCAN_ENGINE)

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "../ui/layout_engine.h"
#include "../ui/router.h"
#include "../ui/project.h"
// every screen's pure core (render + present), shared from ui/
#include "../ui/scan_present.h"
#include "../ui/domain_present.h"
#include "../ui/codex_present.h"
#include "../ui/menu_present.h"
#include "../ui/mesh_present.h"
#include "../ui/omens_present.h"
#include "../ui/echoes_present.h"
#include "../ui/attune_present.h"
#include "../ui/aspect_present.h"
#include "../ui/detail_present.h"
#include "../ui/files_present.h"

namespace engineadapter {
namespace {

Arduino_TFT* gfx_ = nullptr;
int          W_ = 0, H_ = 0;
uint16_t     buf_[480 * 16];               // ONE shared strip buffer for every screen
uint32_t     sliceTransferUs_ = 0;
uinav::Screen cur_ = uinav::Screen::Scan;  // the screen currently composed
uinav::Router router_;                     // navigation authority (remembers DETAIL origin)
scanpres::TouchGate gate_;
// Interaction burst: a screen enter/change, a wake, a theme change, or a touch
// that dispatched a hit all produce a NEW state the user is waiting to see. For
// that one repaint, service() widens its strip budget so the new state paints
// in a single pass (snappy touch) instead of trickling in 4 strips per loop;
// it clears when the frame completes, so idle/animation falls back to the
// steady slice and the single core's capture time is not starved continuously.
bool burst_ = false;
scanpres::PresentMetrics metrics_;         // adapter-level render timing (for /perf)

// one engine per screen -- just scheduler/transition/double Store, no compositor
scanpres::Engine  eScan_;
dompres::DomainEngine eDomain_;
codpres::CodexEngine  eCodex_;
menpres::MenuEngine   eMenu_;
mshpres::MeshEngine   eMesh_;
omnpres::OmenEngine   eOmens_;
echpres::EchoEngine   eEchoes_;
atnpres::AttuneEngine eAttune_;
asppres::AspectEngine eAspect_;
detpres::DetailEngine eDetail_;
flspres::FilesEngine  eFiles_;

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

uinav::Screen toNav(uint8_t sc) {
    switch (sc) {
        case ui::SCR_DOMAIN: return uinav::Screen::Domain;
        case ui::SCR_CODEX:  return uinav::Screen::Codex;
        case ui::SCR_GRIMOIRE: return uinav::Screen::Menu;
        case ui::SCR_MESH:   return uinav::Screen::Mesh;
        case ui::SCR_OMENS:  return uinav::Screen::Omens;
        case ui::SCR_ECHOES: return uinav::Screen::Echoes;
        case ui::SCR_ATTUNE: return uinav::Screen::Attune;
        case ui::SCR_ASPECT: return uinav::Screen::Aspect;
        case ui::SCR_DETAIL: return uinav::Screen::Detail;
        case ui::SCR_FILES:  return uinav::Screen::Files;
        default:             return uinav::Screen::Scan;
    }
}
uint8_t fromNav(uinav::Screen s) {
    switch (s) {
        case uinav::Screen::Domain: return ui::SCR_DOMAIN;
        case uinav::Screen::Codex:  return ui::SCR_CODEX;
        case uinav::Screen::Menu:   return ui::SCR_GRIMOIRE;
        case uinav::Screen::Mesh:   return ui::SCR_MESH;
        case uinav::Screen::Omens:  return ui::SCR_OMENS;
        case uinav::Screen::Echoes: return ui::SCR_ECHOES;
        case uinav::Screen::Attune: return ui::SCR_ATTUNE;
        case uinav::Screen::Aspect: return ui::SCR_ASPECT;
        case uinav::Screen::Detail: return ui::SCR_DETAIL;
        case uinav::Screen::Files:  return ui::SCR_FILES;
        default:                    return ui::SCR_SCAN;
    }
}

// Compose one screen's dirty rows into the shared buffer and push them.
template <class Scene>
void renderSliceT(const Scene& sc, int y0, int y1, void (*cs)(scanr::S&, int, int, const Scene&)) {
    for (int sy = y0; sy < y1; sy += 16) {
        const int sh = (sy + 16 > y1) ? (y1 - sy) : 16;
        scanr::S s{ buf_, W_, sy, sh };
        cs(s, W_, H_, sc);
        sink_.strip(0, sy, W_, sh, buf_);
    }
}

// Build the current screen's device-detail key list + display order, so a CODEX
// row tap maps back to the device list index (nav.selected). Mirrors the CODEX
// projection's ordering exactly.
int codexWindowIndexForRow(const ui::State& s, int row) {
    const int n = s.nSights;
    domr::LocRec rec[ui::MAX_SIGHTS]; char keys[ui::MAX_SIGHTS][20];
    for (int i = 0; i < n; i++) { uiproj::contactKey(keys[i], 20, s.sights[i].band, s.sights[i].mac); rec[i] = { keys[i], s.sights[i].rssi }; }
    int idx[ui::MAX_SIGHTS]; domr::orderLocated(rec, n, idx);
    return (row >= 0 && row < n) ? idx[row] : 0;
}

}  // namespace

void begin(Arduino_TFT* gfx, int W, int H) {
    gfx_ = gfx; W_ = W; H_ = H;
    eScan_.reset(W, H); eDomain_.reset(W, H); eCodex_.reset(W, H); eMenu_.reset(W, H);
    eMesh_.reset(W, H); eOmens_.reset(W, H); eEchoes_.reset(W, H); eAttune_.reset(W, H);
    eAspect_.reset(W, H); eDetail_.reset(W, H); eFiles_.reset(W, H);
    metrics_.clear();
}

void enter(uint8_t uiScreen) {
    cur_ = toNav(uiScreen);
    router_.setScreen(cur_);
    switch (cur_) {
        case uinav::Screen::Scan:   eScan_.enter(); break;
        case uinav::Screen::Domain: eDomain_.enter(); break;
        case uinav::Screen::Codex:  eCodex_.enter(); break;
        case uinav::Screen::Menu:   eMenu_.enter(); break;
        case uinav::Screen::Mesh:   eMesh_.enter(); break;
        case uinav::Screen::Omens:  eOmens_.enter(); break;
        case uinav::Screen::Echoes: eEchoes_.enter(); break;
        case uinav::Screen::Attune: eAttune_.enter(); break;
        case uinav::Screen::Aspect: eAspect_.enter(); break;
        case uinav::Screen::Detail: eDetail_.enter(); break;
        case uinav::Screen::Files:  eFiles_.enter(); break;
    }
    gate_.reset();
    burst_ = true;                              // paint the entered screen in one pass
}
void wake() { eScan_.wake(); gate_.reset(); burst_ = true; }   // only SCAN animates/dims; others re-enter on switch
void themeChanged() {
    // a theme change repaints whatever is current
    switch (cur_) {
        case uinav::Screen::Scan:   eScan_.themeChanged(); break;
        case uinav::Screen::Aspect: eAspect_.themeChanged(); break;
        default: enter(fromNav(cur_)); break;   // force a full repaint via re-enter (sets burst_)
    }
    burst_ = true;
}
void leave() {
    switch (cur_) {
        case uinav::Screen::Scan:   eScan_.leave(); break;
        case uinav::Screen::Domain: eDomain_.leave(); break;
        case uinav::Screen::Codex:  eCodex_.leave(); break;
        case uinav::Screen::Menu:   eMenu_.leave(); break;
        case uinav::Screen::Mesh:   eMesh_.leave(); break;
        case uinav::Screen::Omens:  eOmens_.leave(); break;
        case uinav::Screen::Echoes: eEchoes_.leave(); break;
        case uinav::Screen::Attune: eAttune_.leave(); break;
        case uinav::Screen::Aspect: eAspect_.leave(); break;
        case uinav::Screen::Detail: eDetail_.leave(); break;
        case uinav::Screen::Files:  eFiles_.leave(); break;
    }
    gate_.reset();
    if (gfx_) gfx_->fillScreen(RGB565_BLACK);
}

bool acceptsTouch() {
    switch (cur_) {
        case uinav::Screen::Scan:   return eScan_.acceptsTouch();
        case uinav::Screen::Domain: return eDomain_.acceptsTouch();
        case uinav::Screen::Codex:  return eCodex_.acceptsTouch();
        case uinav::Screen::Menu:   return eMenu_.acceptsTouch();
        case uinav::Screen::Mesh:   return eMesh_.acceptsTouch();
        case uinav::Screen::Omens:  return eOmens_.acceptsTouch();
        case uinav::Screen::Echoes: return eEchoes_.acceptsTouch();
        case uinav::Screen::Attune: return eAttune_.acceptsTouch();
        case uinav::Screen::Aspect: return eAspect_.acceptsTouch();
        case uinav::Screen::Detail: return eDetail_.acceptsTouch();
        case uinav::Screen::Files:  return eFiles_.acceptsTouch();
    }
    return true;
}
bool pending() {
    switch (cur_) {
        case uinav::Screen::Scan:   return eScan_.pending();
        case uinav::Screen::Domain: return eDomain_.pending();
        case uinav::Screen::Codex:  return eCodex_.pending();
        case uinav::Screen::Menu:   return eMenu_.pending();
        case uinav::Screen::Mesh:   return eMesh_.pending();
        case uinav::Screen::Omens:  return eOmens_.pending();
        case uinav::Screen::Echoes: return eEchoes_.pending();
        case uinav::Screen::Attune: return eAttune_.pending();
        case uinav::Screen::Aspect: return eAspect_.pending();
        case uinav::Screen::Detail: return eDetail_.pending();
        case uinav::Screen::Files:  return eFiles_.pending();
    }
    return false;
}

void snapshot(const ui::Nav& nav, const ui::State& s, const scanr::GnssFix& gps,
              scanr::Rec rec, bool hasCell, int batt, uint32_t now) {
    uiproj::Chrome c; c.gps = gps; c.rec = rec; c.hasCell = hasCell; c.batt = batt; c.now = now;
    switch (cur_) {
        case uinav::Screen::Scan:   uiproj::scan(eScan_.back(), s, c);     eScan_.commit();   break;
        case uinav::Screen::Domain: uiproj::domain(eDomain_.back(), s, c); eDomain_.commit(); break;
        case uinav::Screen::Codex:  uiproj::codex(eCodex_.back(), s, c);   eCodex_.commit();  break;
        case uinav::Screen::Menu:   uiproj::menu(eMenu_.back(), s, c);     eMenu_.commit();   break;
        case uinav::Screen::Mesh:   uiproj::mesh(eMesh_.back(), s, c);     eMesh_.commit();   break;
        case uinav::Screen::Omens:  uiproj::omens(eOmens_.back(), s, c);   eOmens_.commit();  break;
        case uinav::Screen::Echoes: uiproj::echoes(eEchoes_.back(), s, c); eEchoes_.commit(); break;
        case uinav::Screen::Attune: uiproj::attune(eAttune_.back(), s, c, nav.setPage, uieng::attuneRowsPerPage(H_)); eAttune_.commit(); break;
        case uinav::Screen::Aspect: uiproj::aspect(eAspect_.back(), s, c); eAspect_.commit(); break;
        case uinav::Screen::Detail: uiproj::detail(eDetail_.back(), s, c); eDetail_.commit(); break;
        case uinav::Screen::Files:  uiproj::files(eFiles_.back(), s, c);   eFiles_.commit();  break;
    }
}

bool service(int budgetStrips) {
    if (!pending()) return false;
    // interaction burst: paint the new state FAST -- but NOT in one giant
    // blocking pass (that opens a touch-blind window and drops taps). Cover the
    // panel in ~2 passes (half the strips each); burst_ stays set until the
    // frame completes (see below), and touch is polled at the TOP of every loop
    // pass, so the panel fills in 2 quick chunks with a touch read in between --
    // snappy and curtain-free without freezing input. Animation keeps the steady
    // 4-strip slice (burst_ is only set on an interaction).
    if (burst_) budgetStrips = H_ / 32 + 2;    // ~half the panel per pass -> ~2 passes
    const uint32_t t0 = micros();
    sliceTransferUs_ = 0;
    bool done = false;
    switch (cur_) {
        case uinav::Screen::Scan:   { auto sl = eScan_.nextSlice(budgetStrips);   if (sl.y1 > sl.y0) renderSliceT<scanr::ScanScene>(eScan_.scene(), sl.y0, sl.y1, &scanr::composeStrip); done = sl.done; break; }
        case uinav::Screen::Domain: { auto sl = eDomain_.nextSlice(budgetStrips); if (sl.y1 > sl.y0) renderSliceT<domr::DomainScene>(eDomain_.scene(), sl.y0, sl.y1, &domr::composeStrip); done = sl.done; break; }
        case uinav::Screen::Codex:  { auto sl = eCodex_.nextSlice(budgetStrips);  if (sl.y1 > sl.y0) renderSliceT<codr::CodexScene>(eCodex_.scene(), sl.y0, sl.y1, &codr::composeStrip); done = sl.done; break; }
        case uinav::Screen::Menu:   { auto sl = eMenu_.nextSlice(budgetStrips);   if (sl.y1 > sl.y0) renderSliceT<menr::MenuScene>(eMenu_.scene(), sl.y0, sl.y1, &menr::composeStrip); done = sl.done; break; }
        case uinav::Screen::Mesh:   { auto sl = eMesh_.nextSlice(budgetStrips);   if (sl.y1 > sl.y0) renderSliceT<mshr::MeshScene>(eMesh_.scene(), sl.y0, sl.y1, &mshr::composeStrip); done = sl.done; break; }
        case uinav::Screen::Omens:  { auto sl = eOmens_.nextSlice(budgetStrips);  if (sl.y1 > sl.y0) renderSliceT<omnr::OmenScene>(eOmens_.scene(), sl.y0, sl.y1, &omnr::composeStrip); done = sl.done; break; }
        case uinav::Screen::Echoes: { auto sl = eEchoes_.nextSlice(budgetStrips); if (sl.y1 > sl.y0) renderSliceT<echr::EchoScene>(eEchoes_.scene(), sl.y0, sl.y1, &echr::composeStrip); done = sl.done; break; }
        case uinav::Screen::Attune: { auto sl = eAttune_.nextSlice(budgetStrips); if (sl.y1 > sl.y0) renderSliceT<atnr::AttuneScene>(eAttune_.scene(), sl.y0, sl.y1, &atnr::composeStrip); done = sl.done; break; }
        case uinav::Screen::Aspect: { auto sl = eAspect_.nextSlice(budgetStrips); if (sl.y1 > sl.y0) renderSliceT<aspr::AspectScene>(eAspect_.scene(), sl.y0, sl.y1, &aspr::composeStrip); done = sl.done; break; }
        case uinav::Screen::Detail: { auto sl = eDetail_.nextSlice(budgetStrips); if (sl.y1 > sl.y0) renderSliceT<detr::DetailScene>(eDetail_.scene(), sl.y0, sl.y1, &detr::composeStrip); done = sl.done; break; }
        case uinav::Screen::Files:  { auto sl = eFiles_.nextSlice(budgetStrips);  if (sl.y1 > sl.y0) renderSliceT<flsr::FilesScene>(eFiles_.scene(), sl.y0, sl.y1, &flsr::composeStrip); done = sl.done; break; }
    }
    const uint32_t total = micros() - t0;
    const uint32_t compose = total > sliceTransferUs_ ? total - sliceTransferUs_ : 0;
    metrics_.slice(compose, sliceTransferUs_);
    if (done) { metrics_.frameDone(); burst_ = false; }   // interaction repaint delivered; back to steady slices
    return done;
}

bool dispatchTouch(ui::Nav& nav, ui::Settings& set, const ui::State& s,
                   bool contact, int panelX, int panelY) {
    if (!gate_.edge(contact, acceptsTouch())) return false;
    router_.setScreen(toNav(nav.screen));
    cur_ = toNav(nav.screen);

    // build the current screen's hit set, with the ctx its rows/threat need
    uieng::HitCtx ctx;
    char codexKeys[ui::MAX_SIGHTS][20]; const char* codexPtr[ui::MAX_SIGHTS];
    static const char* const kAlert = "alert";
    uieng::HitList h;
    switch (cur_) {
        case uinav::Screen::Scan:
            if (s.alertActive) ctx.matchKey = kAlert;
            h = uieng::scanHits(W_, H_, ctx);
            break;
        case uinav::Screen::Domain:
            ctx.hasFix = s.gpsFix;                 // located list has no State source yet -> no rows
            h = uieng::domainHits(W_, H_, ctx);
            break;
        case uinav::Screen::Codex: {
            const int n = s.nSights;
            domr::LocRec rec[ui::MAX_SIGHTS];
            for (int i = 0; i < n; i++) { uiproj::contactKey(codexKeys[i], 20, s.sights[i].band, s.sights[i].mac); rec[i] = { codexKeys[i], s.sights[i].rssi }; }
            int idx[ui::MAX_SIGHTS]; domr::orderLocated(rec, n, idx);
            for (int r = 0; r < n; r++) codexPtr[r] = codexKeys[idx[r]];
            ctx.rowKeys = codexPtr; ctx.nRowKeys = n;
            h = uieng::codexHits(W_, H_, ctx);
            break;
        }
        case uinav::Screen::Menu:   h = uieng::menuHits(W_, H_); break;
        case uinav::Screen::Mesh:   h = uieng::meshHits(W_, H_); break;
        case uinav::Screen::Omens:  h = uieng::omensHits(W_, H_); break;
        case uinav::Screen::Echoes: h = uieng::echoesHits(W_, H_); break;
        case uinav::Screen::Attune: h = uieng::attuneHits(W_, H_, nav.setPage); break;
        case uinav::Screen::Aspect: h = uieng::aspectHits(W_, H_, s.set.theme == pal::TH_COSMICMUFFIN); break;
        case uinav::Screen::Detail: h = uieng::detailHits(W_, H_); break;
        case uinav::Screen::Files: {
            const bool deletable = (nav.fileSel >= 0 && nav.fileSel != nav.filesActive);
            h = uieng::filesHits(W_, H_, nav.fileConfirm, deletable, s.nFileRows);
            break;
        }
    }

    const uint8_t before = nav.screen;
    for (int i = 0; i < h.count; i++) {
        const uieng::Rect r = h.items[i].rect;
        if (panelX < r.x || panelX >= r.x + r.w || panelY < r.y || panelY >= r.y + r.h) continue;
        const uieng::Hit hit = h.items[i];
        const uinav::Requests rq = router_.dispatch(hit);

        // screen first (covers Nav, filter->Codex, located, detail open, Back, OpenMesh)
        nav.screen = fromNav(router_.screen());

        switch (hit.action) {
            case uieng::Action::CodexFilter:       nav.filter = (uint8_t)hit.payload; nav.scroll = 0; break;
            case uieng::Action::OpenLocatedCodex:  nav.scroll = 0; break;
            case uieng::Action::OpenDeviceDetail:
            case uieng::Action::OpenThreatDetail: {
                if (cur_ == uinav::Screen::Codex)
                    nav.selected = (uint16_t)(nav.scroll + codexWindowIndexForRow(s, hit.payload));
                break;   // DETAIL fill is the collector's job, keyed by nav.selected
            }
            case uieng::Action::RangeStep: {
                const int ni = (int)nav.rangeIdx + (hit.payload < 0 ? -1 : 1);
                nav.rangeIdx = (uint8_t)(ni < 0 ? 0 : ni >= domr::kNRanges ? domr::kNRanges - 1 : ni);
                break;
            }
            case uieng::Action::ToggleLabels:      set.labels = set.labels ? 0 : 1; break;
            case uieng::Action::SettingPage:       nav.setPage = (uint8_t)hit.payload; break;
            case uieng::Action::FileSelect:        nav.fileSel = (int8_t)(nav.scroll + hit.payload); break;
            case uieng::Action::FileDelete:        nav.fileConfirm = true; break;
            case uieng::Action::FileConfirmYes:    nav.fileDeleteRequested = true; nav.fileDeleteIdx = nav.fileSel; nav.fileConfirm = false; break;
            case uieng::Action::FileConfirmNo:     nav.fileConfirm = false; break;
            default: break;
        }
        // one-shot side effects + settings steps the router raised
        if (rq.logToggle)    nav.logToggleRequested = true;
        if (rq.meshEnroll)   nav.enrollRequested    = true;
        if (rq.meshReset)    nav.resetNetRequested  = true;
        if (rq.settingReset) nav.resetRequested     = true;
        if (rq.settingStep)  ui::applyKnobStep(set, (rq.settingStep < 0 ? -rq.settingStep : rq.settingStep) - 1, rq.settingStep < 0 ? -1 : 1);
        if (rq.themeStep)    ui::applyAspectStep(set, 0, rq.themeStep);
        if (rq.sweepStep)    ui::applyAspectStep(set, 1, rq.sweepStep);
        if (rq.alertStep)    ui::applyAspectStep(set, 2, rq.alertStep);

        burst_ = true;        // a hit was dispatched -> burst the resulting repaint (snappy touch)
        return nav.screen != before;
    }
    return false;
}

void sampleCapture(uint32_t, uint32_t, uint32_t) { /* reserved: per-screen capture accounting */ }
const scanpres::PresentMetrics& metrics() { return metrics_; }
void clearMetrics() { metrics_.clear(); }

}  // namespace engineadapter

#endif  // WS_C5_35 && UMBRAVOX_SCAN_ENGINE
