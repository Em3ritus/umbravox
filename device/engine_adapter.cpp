// engine_adapter.cpp -- device glue for the whole UI engine. See engine_adapter.h.
//
// Compiled always (device/*.cpp), but its body exists only under the opt-in so
// the default WS build and the NM path carry none of it.
#include "engine_adapter.h"

#if defined(WS_C5_35) && defined(UMBRAVOX_SCAN_ENGINE)

#include <Arduino.h>
#include "poi.h"
#include "logging.h"
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
    if(y0<stagedY0_)stagedY0_=y0;
    if(y1>stagedY1_)stagedY1_=y1;
    for (int sy = y0; sy < y1; sy += 16) {
        const int sh = (sy + 16 > y1) ? (y1 - sy) : 16;
        // Compose hot drawing primitives in internal SRAM, then copy sequentially
        // to PSRAM. Never expose the partially composed frame to the panel.
        scanr::S s{ buf_, W_, sy, sh };
        cs(s, W_, H_, sc);
        if (buffered_) {
            const uint32_t t=micros();
            frame_.stage(sy,buf_,sh);
            sliceCompareUs_ += micros()-t;
        }
        else sink_.strip(0, sy, W_, sh, buf_);
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
    } else if(cur_==uinav::Screen::Domain){const auto& sc=eDomain_.scene();
        for(int i=0;i<sc.nRows && i<ui::MAX_SIGHTS;++i)
            snprintf(visibleKeys_[visibleRows_++],20,"%s",sc.rows[i].key);
    }
}

}  // namespace

void begin(Arduino_TFT* gfx, int W, int H) {
    gfx_ = gfx; W_ = W; H_ = H; stagedY0_=H; stagedY1_=0;
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
        case uinav::Screen::Omens:  eOmens_.enter(); break;
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
    // Compose in bounded slices without exposing an incomplete frame. Only
    // the completed frame is presented; the periodic touch task runs throughout.
    // SPI still updates visible GRAM progressively: this is NOT a page flip.
    if (burst_) budgetStrips = H_ / 32 + 2;    // ~half the panel per pass -> ~2 passes
    const uint32_t t0 = micros();
    sliceTransferUs_ = sliceCompareUs_ = 0;
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
        case uinav::Screen::Mesh:   h = uieng::meshHits(W_, H_); break;
        case uinav::Screen::Omens:  h = uieng::omensHits(W_, H_); break;
        case uinav::Screen::Echoes: h = uieng::echoesHits(W_, H_); break;
        case uinav::Screen::Attune: h = uieng::attuneHits(W_, H_, nav.setPage); break;
        case uinav::Screen::Aspect: h = uieng::aspectHits(W_, H_, s.set.theme == pal::TH_COSMICMUFFIN); break;
        case uinav::Screen::Detail: h = uieng::detailHits(W_, H_,nav.detailPage==2 && s.detail.band!=ui::BAND_BLE); break;
        case uinav::Screen::Files: {
            const bool deletable = (nav.fileSel >= 0 && nav.fileSel != nav.filesActive);
            h = uieng::filesHits(W_, H_, nav.fileConfirm, deletable, s.nFileRows);
            break;
        }
    }

    scanpres::recordingHits(h, sceneRec_);
    if (!pendingHit_.pending()) {
        inputevents::Press p;
        if (touch::takePress(p) && p.epoch == inputEpoch_)
            pendingHit_.observe(h, p.x, p.y, p.epoch);
    }
    const uint8_t before = nav.screen;
    uieng::Hit hit{};
    if (pendingHit_.take(acceptsTouch(), inputEpoch_, h, hit)) {
        const uinav::Requests rq = router_.dispatch(hit);

        // screen first (covers Nav, filter->Codex, located, detail open, Back, OpenMesh)
        nav.screen = fromNav(router_.screen());

        switch (hit.action) {
            case uieng::Action::MeshPage: nav.meshPage=(nav.meshPage+1)%(s.nNodes+1);break;
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
            case uieng::Action::ToggleLabels:      set.labels = set.labels ? 0 : 1; break;
            case uieng::Action::SettingPage:       nav.setPage = (uint8_t)hit.payload; break;
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

void sampleCapture(uint32_t frames, uint32_t uniques, uint32_t ble) { metrics_.capture(frames,uniques,ble); }
const scanpres::PresentMetrics& metrics() { return metrics_; }
void clearMetrics() { metrics_.clear(); }

}  // namespace engineadapter

#endif  // WS_C5_35 && UMBRAVOX_SCAN_ENGINE
