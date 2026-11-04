// engine_adapter.cpp -- device glue for the whole UI engine. See engine_adapter.h.
//
// Compiled always (device/*.cpp), but its body exists only under the opt-in so
// legacy targets carry none of it.
#include "engine_adapter.h"

#if defined(UMBRAVOX_SCAN_ENGINE)

#include <Arduino.h>
#include "poi.h"
#include "ble_bridge.h"
#include "logging.h"
#include "alertlog.h"
#include <esp_heap_caps.h>
#include "touch.h"
#include "../ui/pending_hit.h"
#include "../ui/frame_present.h"
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
#include "../ui/files.h"

namespace engineadapter {
namespace {

Arduino_TFT* gfx_ = nullptr;
int          W_ = 0, H_ = 0;
uint16_t     buf_[480 * 16];               // ONE shared strip buffer for every screen
uint32_t     sliceTransferUs_ = 0, sliceCompareUs_ = 0;
uinav::Screen cur_ = uinav::Screen::Scan;  // the screen currently composed
uinav::Router router_;                     // navigation authority (remembers DETAIL origin)
scanr::Rec sceneRec_ = scanr::Rec::Off;
inputevents::PendingHit pendingHit_;
settingsctl::ForgetArm forgetArm_;   // two-tap confirm for PAIRED PHONES
bool holdToBridge_ = false;   // a long press on the header bridge chip is waiting to navigate
uint32_t inputEpoch_ = 0;
framepresent::Frame frame_;
bool buffered_ = false;
int stagedY0_=0, stagedY1_=0;
bool entryTiming_=false;
uint32_t entryAt_=0;
// Interaction burst: a screen enter/change, a wake, a theme change, or a touch
// that dispatched a hit all produce a NEW state the user is waiting to see. For
// that one repaint, service() widens its composition budget. The staged frame
// stays invisible until composition completes, then changed regions transfer;
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
ui::Omen alertDetail_;
bool alertDetailOpen_=false;
int alertDetailPage_=0;
echpres::EchoEngine   eEchoes_;
atnpres::AttuneEngine eAttune_;
asppres::AspectEngine eAspect_;
displayctl::State display_;
settingsctl::State settings_;
int pendingSensitivity_=-1;
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
void renderSliceT(const Scene& sc, int y0, int y1, void (*cs)(scanr::S&, int, int, const Scene&), int wlo = 0, int whi = -1) {
    if(y0<stagedY0_)stagedY0_=y0;
    if(y1>stagedY1_)stagedY1_=y1;
    // A column window (sweep-only frame) composes and stages just those columns, but only against a valid
    // panel mirror: with no mirror the whole frame must be repainted.
    const bool win = whi > wlo && (!buffered_ || frame_.valid());
    for (int sy = y0; sy < y1; sy += 16) {
        const int sh = (sy + 16 > y1) ? (y1 - sy) : 16;
        // Compose hot drawing primitives in internal SRAM, then copy sequentially
        // to PSRAM. Never expose the partially composed frame to the panel.
        scanr::S s = win ? scanr::S{ buf_, W_, sy, sh, wlo, whi } : scanr::S{ buf_, W_, sy, sh };
        cs(s, W_, H_, sc);
        if (buffered_) {
            const uint32_t t=micros();
            if (win) frame_.stageWindow(sy,buf_,sh,wlo,whi-wlo);
            else frame_.stage(sy,buf_,sh);
            sliceCompareUs_ += micros()-t;
        }
        else if (win) sink_.strip(wlo, sy, whi-wlo, sh, buf_);
        else sink_.strip(0, sy, W_, sh, buf_);
    }
}

// SCAN on a wide panel: compose and stage only the dirty REGIONS (count tiles, channel readout, spectrum,
// radar or its wedge, header and nav bands) that touch each strip, instead of full-width rows. Needs a valid
// panel mirror: with none, repaint the whole strip.
void renderScanRegions(const scanr::ScanScene& sc, int y0, int y1, const uieng::Rect* rg, int nr) {
    if(y0<stagedY0_)stagedY0_=y0;
    if(y1>stagedY1_)stagedY1_=y1;
    const bool ok = !buffered_ || frame_.valid();
    for (int sy = y0; sy < y1; sy += 16) {
        const int sh = (sy + 16 > y1) ? (y1 - sy) : 16;
        if (!ok) {                                           // no mirror to diff against: full-width strip
            scanr::S s{ buf_, W_, sy, sh };
            scanr::composeStrip(s, W_, H_, sc);
            if (buffered_) frame_.stage(sy, buf_, sh); else sink_.strip(0, sy, W_, sh, buf_);
            continue;
        }
        for (int i = 0; i < nr; ++i) {
            if (rg[i].y >= sy + sh || rg[i].y + rg[i].h <= sy) continue;
            const int lo = rg[i].x < 0 ? 0 : rg[i].x, hi = rg[i].x + rg[i].w > W_ ? W_ : rg[i].x + rg[i].w;
            if (hi <= lo) continue;
            scanr::S s{ buf_, W_, sy, sh, lo, hi };          // buf_ holds just this region's columns, packed
            scanr::composeStrip(s, W_, H_, sc);
            if (buffered_) {
                const uint32_t t=micros();
                frame_.stageRegion(sy, buf_, sh, lo, hi-lo);
                sliceCompareUs_ += micros()-t;
            } else sink_.strip(lo, sy, hi-lo, sh, buf_);
        }
    }
}

// Bind taps to the rows actually presented, not the next live collection.
char visibleKeys_[ui::MAX_SIGHTS][20]{};
int visibleRows_=0;
void saveVisibleRows() {
    visibleRows_=0;
    if(cur_==uinav::Screen::Codex){const auto& sc=eCodex_.scene();
        for(int i=0;i<sc.nRows && i<ui::MAX_SIGHTS;++i)
            snprintf(visibleKeys_[visibleRows_++],20,"%s",sc.rows[i].key);
    } else if(cur_==uinav::Screen::Omens){const auto& sc=eOmens_.scene();
        if(!sc.detail)for(int i=0;i<sc.nRows && i<ui::MAX_SIGHTS;++i)
            snprintf(visibleKeys_[visibleRows_++],20,"%s",sc.rows[i].key);
    } else if(cur_==uinav::Screen::Domain){const auto& sc=eDomain_.scene();
        for(int i=0;i<sc.nRows && i<ui::MAX_SIGHTS;++i)
            snprintf(visibleKeys_[visibleRows_++],20,"%s",sc.rows[i].key);
    }
}

}  // namespace

void begin(Arduino_TFT* gfx, int W, int H) {
    gfx_ = gfx; W_ = W; H_ = H; stagedY0_=H; stagedY1_=0;
    {   // The header bridge chip: tap toggles the bridge, a hold jumps to its settings card.
        const auto chip = uieng::chrome(W, H).bridge;
        touch::setHoldTarget(chip.x, chip.y, chip.w, chip.h);
    }
    auto* front = static_cast<uint16_t*>(heap_caps_malloc(W*H*2, MALLOC_CAP_SPIRAM));
    auto* back = static_cast<uint16_t*>(heap_caps_malloc(W*H*2, MALLOC_CAP_SPIRAM));
    buffered_ = front && back;
    if (buffered_) frame_.begin(front, back, W, H);
    else { free(front); free(back); Serial.println("ui: frame allocation failed; progressive fallback"); }
    eScan_.reset(W, H); eDomain_.reset(W, H); eCodex_.reset(W, H); eMenu_.reset(W, H);
    eMesh_.reset(W, H); eOmens_.reset(W, H); eEchoes_.reset(W, H); eAttune_.reset(W, H);
    eAspect_.reset(W, H); eDetail_.reset(W, H); eFiles_.reset(W, H);
    metrics_.clear();
}

void enter(uint8_t uiScreen) {
    entryTiming_=true; entryAt_=millis();
    cur_ = toNav(uiScreen);
    router_.setScreen(cur_);
    switch (cur_) {
        case uinav::Screen::Scan:   eScan_.enter(); break;
        case uinav::Screen::Domain: eDomain_.enter(); break;
        case uinav::Screen::Codex:  eCodex_.enter(); break;
        case uinav::Screen::Menu:   eMenu_.enter(); break;
        case uinav::Screen::Mesh:   eMesh_.enter(); break;
        case uinav::Screen::Omens:  alertDetailOpen_=false;alertDetailPage_=0;eOmens_.enter(); break;
        case uinav::Screen::Echoes: eEchoes_.enter(); break;
        case uinav::Screen::Attune: eAttune_.enter(); break;
        case uinav::Screen::Aspect: eAspect_.enter(); break;
        case uinav::Screen::Detail: eDetail_.enter(); break;
        case uinav::Screen::Files:  eFiles_.enter(); break;
    }
    pendingHit_.cancel(); ++inputEpoch_; touch::newScreen();
    burst_ = true;                              // paint the entered screen fast (2-pass burst)
}
void wake() { frame_.invalidate(); enter(fromNav(cur_)); }   // wake the current screen, including MENU and sub-screens
void themeChanged() {
    // a theme change repaints whatever is current
    switch (cur_) {
        case uinav::Screen::Scan:   eScan_.themeChanged(); break;
        case uinav::Screen::Aspect: eAspect_.themeChanged(); break;
        default: enter(fromNav(cur_)); break;   // force a full repaint via re-enter (sets burst_)
    }
    burst_ = true;
}
void cancelInput() { pendingHit_.cancel(); touch::cancelPresses(); }
void invalidatePanel() { frame_.invalidate(); }

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
    pendingHit_.cancel(); ++inputEpoch_; touch::newScreen();
    frame_.invalidate();
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
              scanr::Rec rec, bool hasCell, int batt, uint32_t now, bool pmuOk, bool charging) {
    sceneRec_ = rec;
    uiproj::Chrome c; c.gps = gps; c.rec = rec; c.hasCell = hasCell; c.batt = batt; c.now = now; c.pmuOk = pmuOk; c.charging = charging; c.poi = poi::status(now);
    switch (cur_) {
        case uinav::Screen::Scan:   uiproj::scan(eScan_.back(), s, c); eScan_.back().animate(true); eScan_.commit();   break;
        case uinav::Screen::Domain: uiproj::domain(eDomain_.back(), s, c, W_, H_); eDomain_.commit(); break;
        case uinav::Screen::Codex:  uiproj::codex(eCodex_.back(), s, c, W_, H_);   eCodex_.commit();  break;
        case uinav::Screen::Menu:   uiproj::menu(eMenu_.back(), s, c);     eMenu_.commit();   break;
        case uinav::Screen::Mesh:   uiproj::mesh(eMesh_.back(), s, c);     eMesh_.commit();   break;
        case uinav::Screen::Omens:  uiproj::omens(eOmens_.back(), s, c); if(alertDetailOpen_)eOmens_.back().detail(alertDetail_,alertDetailPage_); eOmens_.commit();  break;
        case uinav::Screen::Echoes: uiproj::echoes(eEchoes_.back(), s, c); eEchoes_.commit(); break;
        case uinav::Screen::Attune: {
            uiproj::attune(eAttune_.back(),s,c,0,1);
            settingsctl::Data d;d.view=settings_;d.settings=s.set;d.netId=s.netId;
            const auto bs=blebridge::stats();d.connected=bs.connected;d.streaming=bs.streaming;d.pin=bs.pin;d.phones=bs.bonds;d.forgetArmed=forgetArm_.armed(millis());
            snprintf(d.name,sizeof d.name,"%s",blebridge::deviceName());
            snprintf(d.pcap,sizeof d.pcap,"%s",s.pcapStatus);
            eAttune_.back().controls(d);eAttune_.commit();break;
        }
        case uinav::Screen::Aspect:
            if(display_.editor!=6)display_.sensitivity=touch::sensitivity();
            if(display_.editor==4){const auto p=touch::observed();displayctl::sample(display_,W_,H_,p.any,p.panelX,p.panelY);}
            uiproj::aspect(eAspect_.back(), s, c);eAspect_.back().controls(display_,s.set);eAspect_.commit();break;
        case uinav::Screen::Detail: uiproj::detail(eDetail_.back(), s, c); eDetail_.commit(); break;
        case uinav::Screen::Files:  uiproj::files(eFiles_.back(), s, c);   eFiles_.commit();  break;
    }
}

bool service(int budgetStrips) {
    if (!pending()) return false;
    // Compose in bounded slices without exposing an incomplete frame. Only
    // the completed frame is presented; the periodic touch task runs throughout.
    // SPI still updates visible GRAM progressively: this is NOT a page flip.
    if (burst_) budgetStrips = H_ / 32 + 2;    // ~half the panel per pass -> ~2 passes
    const uint32_t t0 = micros();
    sliceTransferUs_ = sliceCompareUs_ = 0;
    bool done = false;
    switch (cur_) {
        case uinav::Screen::Scan:   { auto sl = eScan_.nextSlice(budgetStrips);
            uieng::Rect rg[scanpres::Engine::kMaxRegions]; const int nr = eScan_.regions(rg, scanpres::Engine::kMaxRegions);
            if (nr > 0) { if (sl.y1 > sl.y0) renderScanRegions(eScan_.scene(), sl.y0, sl.y1, rg, nr); }
            else { int wl=0,wh=-1; if(!eScan_.window(wl,wh)){wl=0;wh=-1;} if (sl.y1 > sl.y0) renderSliceT<scanr::ScanScene>(eScan_.scene(), sl.y0, sl.y1, &scanr::composeStrip, wl, wh); }
            done = sl.done; break; }
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
    const uint32_t other = sliceTransferUs_ + sliceCompareUs_;
    const uint32_t compose = total > other ? total - other : 0;
    const uint32_t presentationStart=micros();
    const uint32_t earlyTransfer=sliceTransferUs_;
    if (done && buffered_) {
        frame_.present(cur_ == uinav::Screen::Menu, [](uieng::Rect r, const uint16_t* px, int stride) {
            const uint32_t t = micros();
            gfx_->startWrite();
            gfx_->writeAddrWindow(r.x, r.y, r.w, r.h);
            // Fully composed rectangle, one window; no intervening loop/GPS.
            for (int y=r.y; y<r.y+r.h; ++y) {
                // GFX's C5 SPI writer reads pairs even for an odd pixel count.
                // Pad the internal bounce row; never over-read the PSRAM frame.
                memcpy(buf_, px+y*stride+r.x, r.w*sizeof(uint16_t));
                buf_[r.w] = 0;
                gfx_->writePixels(buf_, r.w);
            }
            gfx_->endWrite();
            sliceTransferUs_ += micros()-t;
        },stagedY0_,stagedY1_);
    }
    const uint32_t diff=sliceCompareUs_+micros()-presentationStart-(sliceTransferUs_-earlyTransfer);
    metrics_.work(compose,diff,sliceTransferUs_);
    if (done) {
        saveVisibleRows();
        stagedY0_=H_;stagedY1_=0;
        if(entryTiming_) {
            const uint32_t age=millis()-entryAt_;
            if(age>metrics_.entryMaxMs)metrics_.entryMaxMs=age;
            entryTiming_=false;
        }
        metrics_.frameDone(); burst_ = false;
    }   // interaction repaint delivered; back to steady slices
    return done;
}

bool dispatchTouch(ui::Nav& nav, ui::Settings& set, const ui::State& s) {
    router_.setScreen(toNav(nav.screen));
    cur_ = toNav(nav.screen);

    // build the current screen's hit set, with the ctx its rows/threat need
    uieng::HitCtx ctx;
    const char* rowKeys[ui::MAX_SIGHTS]{};
    int rowCount=visibleRows_;
    for(int i=0;i<rowCount;++i)rowKeys[i]=visibleKeys_[i];
    if(!acceptsTouch()) { // entry presses defer against the incoming scene
        rowCount=0;
        if(cur_==uinav::Screen::Codex){const auto& sc=eCodex_.scene();rowCount=sc.nRows;
            for(int i=0;i<rowCount;++i)rowKeys[i]=sc.rows[i].key;}
        if(cur_==uinav::Screen::Omens){const auto& sc=eOmens_.scene();rowCount=sc.nRows;
            for(int i=0;i<rowCount;++i)rowKeys[i]=sc.rows[i].key;}
        if(cur_==uinav::Screen::Domain){const auto& sc=eDomain_.scene();rowCount=sc.nRows;
            for(int i=0;i<rowCount;++i)rowKeys[i]=sc.rows[i].key;}
    }
    static const char* const kAlert = "alert";
    uieng::HitList h;
    switch (cur_) {
        case uinav::Screen::Scan:
            if (s.alertActive) ctx.matchKey = kAlert;
            h = uieng::scanHits(W_, H_, ctx);
            break;
        case uinav::Screen::Domain:
            ctx.hasFix = rowCount>0; // historic located rows remain usable without a current fix
            ctx.rowKeys=rowKeys;ctx.nRowKeys=rowCount;
            h=uieng::domainHits(W_,H_,ctx);break;
        case uinav::Screen::Codex:
            ctx.rowKeys=rowKeys;ctx.nRowKeys=rowCount;
            h=uieng::codexHits(W_,H_,ctx);break;
        case uinav::Screen::Menu:   h = uieng::menuHits(W_, H_); break;
        case uinav::Screen::Mesh:   h = uieng::meshHits(W_, H_, nav.meshPage, s.nNodes); break;
        case uinav::Screen::Omens: {
            const auto& sc=eOmens_.scene();
            h=uieng::omensHits(W_,H_,rowKeys,rowCount,sc.detail!=nullptr);break;
        }
        case uinav::Screen::Echoes: h = uieng::echoesHits(W_, H_); break;
        case uinav::Screen::Attune: h = settingsctl::hits(W_, H_, settings_); break;
        case uinav::Screen::Aspect: h = displayctl::hits(W_, H_, display_, s.set.theme == pal::TH_COSMICMUFFIN); break;
        case uinav::Screen::Detail: h = uieng::detailHits(W_, H_,nav.detailPage==2 && s.detail.band!=ui::BAND_BLE); break;
        case uinav::Screen::Files: {
            const bool deletable = (nav.fileSel >= 0 && nav.fileSel != nav.filesActive);
            h = uieng::filesHits(W_, H_, nav.fileConfirm, deletable, s.nFileRows, nav.filesN, nav.scroll);
            break;
        }
    }

    scanpres::recordingHits(h, sceneRec_);
    if (!pendingHit_.pending()) {
        inputevents::Press p;
        if (touch::takePress(p) && p.epoch == inputEpoch_) {
            if (p.hold) holdToBridge_ = true;   // long press on the header chip
            else pendingHit_.observe(h, p.x, p.y, p.epoch);
        }
    }
    const uint8_t before = nav.screen;
    uieng::Hit hit{};
    bool haveHit = false;
    if (holdToBridge_ && acceptsTouch()) {
        holdToBridge_ = false;
        hit.slot = "bridge-hold"; hit.action = uieng::Action::Nav; hit.payload = int(uieng::ScreenId::Attune);
        haveHit = true;
    } else {
        haveHit = pendingHit_.take(acceptsTouch(), inputEpoch_, h, hit);
    }
    if (haveHit) {
        const uinav::Requests rq = router_.dispatch(hit);

        // screen first (covers Nav, filter->Codex, located, detail open, Back, OpenMesh)
        nav.screen = fromNav(router_.screen());

        switch (hit.action) {
            case uieng::Action::Nav:
                display_.editor=-1;display_.count=0;settings_.editor=-1;
                if(nav.screen==ui::SCR_FILES)ui::filesEnter(nav);
                if(hit.slot && !strcmp(hit.slot,"node-tune"))settings_.tab=3;
                if(hit.slot && !strcmp(hit.slot,"bridge-hold"))settings_.tab=2;   // ATTUNE > BRIDGE
                break;
            case uieng::Action::MeshPage: nav.meshPage=uieng::meshNavigate(nav.meshPage,s.nNodes,hit.payload);break;
            case uieng::Action::DetailPage: nav.detailPage=(nav.detailPage+1)%(s.detail.band==ui::BAND_BLE?2:3); break;
            case uieng::Action::EchoPage: {
                const int per=uieng::echoes(W_,H_).nRows;
                nav.scroll=uiproj::pageOffset(s.echoTotal,s.scroll+(hit.payload<0?-per:per),per);break;
            }
            case uieng::Action::ClientPage: {
                const int per=uieng::detailClients(W_,H_).nRows;
                nav.clientOffset=uiproj::pageOffset(s.clients.total,s.clients.offset+(hit.payload<0?-per:per),per);break;
            }
            case uieng::Action::CodexFilter:       nav.filter = (uint8_t)hit.payload; nav.scroll = 0; break;
            case uieng::Action::OpenLocatedCodex:  nav.filter=ui::F_LOCATED; nav.scroll = 0; break;
            case uieng::Action::CodexPage: {
                const int n=uieng::codex(W_,H_).nRows;
                nav.scroll=uiproj::pageOffset(s.totalSights,nav.scroll+(hit.payload<0?-n:n),n);
                break;
            }
            case uieng::Action::OpenDeviceDetail:
            case uieng::Action::OpenThreatDetail: {
                nav.detailPage=0;nav.clientOffset=0;
                snprintf(nav.detailKey,sizeof nav.detailKey,"%s",hit.key?hit.key:"");
                break;   // DETAIL fill resolves the address, never a shifting list index
            }
            case uieng::Action::RangeStep: {
                nav.rangeIdx = (uint8_t)domr::rangeIndexStep(nav.rangeIdx,hit.payload);
                break;
            }
            case uieng::Action::SettingsControl:
                if(hit.payload==settingsctl::FORGET_PHONES){
                    if(forgetArm_.tap(millis()))blebridge::forgetPhones();   // second tap within ARM_MS confirms
                }else settingsctl::apply(settings_,set,hit.payload);
                cancelInput();break;
            case uieng::Action::DisplayControl: {
                const int p=hit.payload;display_.error=false;
                if(p>=0&&p<3){display_.tab=p;display_.editor=-1;}
                else if(p>=10&&p<=21){
                    const int id=p-10;
                    if(id==11)set.labels=!set.labels;
                    else {display_.editor=id;display_.count=0;display_.down=false;display_.sensitivity=touch::sensitivity();}
                }else if(p==30||p==31){
                    const int dir=p==30?-1:1;
                    displayctl::step(display_,set,dir);
                    if(display_.editor>=8&&display_.editor<=10)ui::applyAspectStep(set,display_.editor-8,dir);
                }else if(p==32){
                    switch(display_.editor){
                        case 0:set.bright=80;break;case 1:set.idleIdx=2;break;case 2:set.idleMode=ui::IDLE_DIM;break;
                        case 4:display_.count=0;break;case 6:display_.sensitivity=12;break;
                        case 8:set.theme=0;break;case 9:set.sweep=0;break;case 10:set.alertStyle=0;break;
                    }
                    ui::clampSettings(set);
                }else if(p==33){
                    if(display_.editor==6)pendingSensitivity_=display_.sensitivity;
                    else display_.editor=-1;
                }
                // Do not let retained presses from the previous subpage activate its replacement.
                cancelInput();break;
            }
            case uieng::Action::ToggleLabels:      set.labels = set.labels ? 0 : 1; break;
            case uieng::Action::SettingPage:       nav.setPage = (uint8_t)hit.payload; break;
            case uieng::Action::AlertTest: alertlog::pushTest(millis());nav.scroll=0;cancelInput();break;
            case uieng::Action::AlertOpen: {
                uint32_t id=0;if(hit.key)for(const char* p=hit.key;*p;p++){if(*p<'0'||*p>'9'){id=0;break;}id=id*10+uint32_t(*p-'0');}
                alertDetailOpen_=alertlog::find(id,millis(),alertDetail_);alertDetailPage_=0;cancelInput();break;
            }
            case uieng::Action::AlertClose:alertDetailOpen_=false;cancelInput();break;
            case uieng::Action::AlertPage: {if(alertDetailOpen_){
                int pages=omnr::detailPageCount(W_,H_,eOmens_.scene().detail);
                alertDetailPage_+=hit.payload<0?-1:1;if(alertDetailPage_<0)alertDetailPage_=0;if(alertDetailPage_>=pages)alertDetailPage_=pages>0?pages-1:0;
                cancelInput();break;
            }const int per=uieng::omens(W_,H_).nRows;
                nav.scroll=uiproj::pageOffset(alertlog::count(),nav.scroll+(hit.payload<0?-per:per),per);cancelInput();break;}
            case uieng::Action::FileRefresh: nav.filesScanRequested=true;cancelInput();break;
            case uieng::Action::FilePage: {
                const int per=uieng::files(W_,H_).nRows;
                nav.scroll=uiproj::pageOffset(nav.filesN,nav.scroll+(hit.payload<0?-per:per),per);
                nav.fileSel=-1;nav.fileConfirm=false;cancelInput();break;
            }
            case uieng::Action::FileSelect:        nav.fileSel = (int8_t)(nav.scroll + hit.payload); break;
            case uieng::Action::FileDelete:        nav.fileConfirm = true; break;
            case uieng::Action::FileConfirmYes:    nav.fileDeleteRequested = true; nav.fileDeleteIdx = nav.fileSel; nav.fileConfirm = false; break;
            case uieng::Action::FileConfirmNo:     nav.fileConfirm = false; break;
            default: break;
        }
        // one-shot side effects + settings steps the router raised
        if (rq.markPoi) { const uint32_t now=millis(); poi::request(ui::Gnss::fix(),now,ui::Gnss::utcEpoch(now),logging::fileName()); }
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

// Run outside dispatchTouch's hit-list/row-key stack frame.
void serviceSettings(){
    if(pendingSensitivity_<0)return;
    const int value=pendingSensitivity_;pendingSensitivity_=-1;
    if(touch::setSensitivity(value)){display_.error=false;display_.editor=-1;}
    else display_.error=true;
}

void sampleCapture(uint32_t frames, uint32_t uniques, uint32_t ble) { metrics_.capture(frames,uniques,ble); }
const scanpres::PresentMetrics& metrics() { return metrics_; }
void clearMetrics() { metrics_.clear(); }

}  // namespace engineadapter

#endif  // UMBRAVOX_SCAN_ENGINE
