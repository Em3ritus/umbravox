// domain_present.h -- the DOMAIN device adapter's pure core.
//
// Mirrors scan_present.h for the DOMAIN screen. The generic machinery -- the
// resumable bounded StripScheduler, the Transition/touch gate, the TouchGate
// edge detector, the PresentMetrics accumulator and the idle-suspend drain
// policy -- is SHARED from scanpres:: rather than duplicated; only the pieces
// that are DOMAIN-specific live here:
//
//   DomainStore  -- a STABLE snapshot of a DomainScene. The map dots, track
//                   points and the located ROWS (with their key/label strings)
//                   all live in owned storage, so the scene handed to the
//                   compositor cannot move under a sliced redraw.
//   sceneDirty   -- which display bands (status chrome / body) changed.
//   FrozenOrder  -- the open-decision-#5 guarantee: the located list's row
//                   order is snapshotted at touch-down and held until release,
//                   so no row reshuffles under the finger.
//   DomainEngine -- the entry->commit->service sequencer (the scan Engine's
//                   twin for DomainScene).
//
// Host and device compilable (no Arduino deps).
#pragma once
#include "domain_render.h"
#include "scan_present.h"     // scanpres:: StripScheduler/Transition/TouchGate/PresentMetrics
#include "model.h"            // ui::MAX_PLOT / MAX_TRACK / SSID_MAX for the store caps
#include <cstdint>

namespace dompres {

using domr::DomainScene;
using domr::MapDot;
using domr::TrackDot;
using domr::DomRow;

// Re-export the shared, screen-agnostic machinery so a DOMAIN caller uses one
// namespace. These are literally the SCAN engine's pieces -- no second copy.
using scanpres::StripScheduler;
using scanpres::Transition;
using scanpres::TouchGate;
using scanpres::PresentMetrics;
using scanpres::serviceWhileSuspended;

// --- stable scene snapshot ----------------------------------------------------
// Everything the scene points at is copied into owned storage: the pre-projected
// map dots and track points, and the located rows together with their key/label
// bytes. The scene stays valid and unchanging for the whole sliced redraw.
class DomainStore {
public:
    static constexpr int MAXDOTS  = ui::MAX_PLOT;    // 64
    static constexpr int MAXTRACK = ui::MAX_TRACK;   // 24
    static constexpr int MAXROWS  = 8;               // a wide page is 2; headroom for CODEX reuse
    static constexpr int STRMAX   = ui::AP_SSID_MAX;    // 33

    void begin(uint8_t theme, uint32_t now) {
        nd_ = nt_ = nr_ = 0;
        sc_ = DomainScene{};
        sc_.theme = theme; sc_.now = now;
        sc_.dots = dots_; sc_.nDots = 0;
        sc_.track = track_; sc_.nTrack = 0;
        sc_.rows = rows_; sc_.nRows = 0;
    }
    void gps(const domr::GnssFix& f) { sc_.gps = f; }
    void storage(ui::StorageState sd){sc_.sd=sd;}
    void header(bool bridge, bool pmu, bool charging) { sc_.bridgeOn=bridge; sc_.pmuOk=pmu; sc_.charging=charging; }
    void poi(ui::PoiStatus value) { sc_.poi=value; }
    void status(int nodes, bool hasCell, int batt, domr::Rec rec) {
        sc_.nodes = nodes; sc_.hasCell = hasCell; sc_.battPct = batt; sc_.rec = rec;
    }
    void counts(int shown, int total, int beyond, int rangeM) {
        sc_.plotShown = shown; sc_.plotTotal = total; sc_.plotBeyond = beyond; sc_.rangeM = rangeM;
    }
    void page(int lo, int hi, int total) { sc_.pageLo = lo; sc_.pageHi = hi; sc_.listTotal = total; }

    bool addDot(int dx, int dy, uint8_t band, bool threat) {
        if (nd_ >= MAXDOTS) return false;
        dots_[nd_++] = MapDot{ dx, dy, band, threat }; sc_.nDots = nd_; return true;
    }
    bool addTrack(int dx, int dy) {
        if (nt_ >= MAXTRACK) return false;
        track_[nt_++] = TrackDot{ dx, dy }; sc_.nTrack = nt_; return true;
    }
    // Append a located row (key + label copied in). Returns false when full.
    bool addRow(const char* key, const char* label, uint8_t band, int rssi=0, int chan=0, const char* mac="") {
        if (nr_ >= MAXROWS) return false;
        copy_(keyBuf_[nr_], STRMAX, key);
        copy_(lblBuf_[nr_], STRMAX, label);
        copy_(macBuf_[nr_], 18, mac);
        rows_[nr_] = DomRow{ keyBuf_[nr_], lblBuf_[nr_], band, rssi, chan, macBuf_[nr_] };
        nr_++; sc_.nRows = nr_; return true;
    }

    const DomainScene& scene() const { return sc_; }
    int dotCount() const { return nd_; }
    int rowCount() const { return nr_; }

private:
    static void copy_(char* dst, int cap, const char* src) {
        int k = 0; for (; src && src[k] && k < cap - 1; k++) dst[k] = src[k]; dst[k] = 0;
    }
    MapDot   dots_[MAXDOTS];
    TrackDot track_[MAXTRACK];
    DomRow   rows_[MAXROWS];
    char     keyBuf_[MAXROWS][STRMAX], lblBuf_[MAXROWS][STRMAX], macBuf_[MAXROWS][18];
    int      nd_ = 0, nt_ = 0, nr_ = 0;
    DomainScene sc_;
};

// --- scene diff: which display bands changed ---------------------------------
// `status` is the top chrome (GPS/mesh/batt/log), reusing the SCAN label
// projections so the fresh/stale/acquiring boundary is identical. `body` is the
// map (dots/track/range/counts) + the contacts column (page window + rows). The
// nav bar is static and never reported. Pure.
inline void sceneDirty(const DomainScene& a, const DomainScene& b, bool& status, bool& body) {
    char la[16], lb[16];
    const bool gpsSame = scanr::gpsLabel(a.gps, la, 16) == scanr::gpsLabel(b.gps, lb, 16)
                         && !__builtin_strcmp(la, lb);
    char ra[16], rb[16];
    scanr::ScanScene sa; sa.rec = a.rec; scanr::ScanScene sb; sb.rec = b.rec;
    const bool recSame = scanr::recLabel(sa, ra, 16) == scanr::recLabel(sb, rb, 16)
                         && !__builtin_strcmp(ra, rb);
    status = !gpsSame || !recSame || a.nodes != b.nodes
             || a.hasCell != b.hasCell || a.battPct != b.battPct || a.bridgeOn != b.bridgeOn || a.pmuOk != b.pmuOk || a.charging != b.charging || a.sd != b.sd;

    bool mapSame = a.rangeM == b.rangeM && a.plotShown == b.plotShown
                   && a.plotTotal == b.plotTotal && a.plotBeyond == b.plotBeyond
                   && a.nDots == b.nDots && a.nTrack == b.nTrack;
    for (int i = 0; mapSame && i < a.nDots; i++)
        mapSame = a.dots[i].dx == b.dots[i].dx && a.dots[i].dy == b.dots[i].dy
                  && a.dots[i].band == b.dots[i].band && a.dots[i].threat == b.dots[i].threat;
    for (int i = 0; mapSame && i < a.nTrack; i++)
        mapSame = a.track[i].dx == b.track[i].dx && a.track[i].dy == b.track[i].dy;

    bool listSame = a.nRows == b.nRows && a.pageLo == b.pageLo
                    && a.pageHi == b.pageHi && a.listTotal == b.listTotal;
    for (int i = 0; listSame && i < a.nRows; i++)
        listSame = a.rows[i].band == b.rows[i].band && a.rows[i].rssi == b.rows[i].rssi && a.rows[i].chan == b.rows[i].chan
                   && !__builtin_strcmp(a.rows[i].mac, b.rows[i].mac)
                   && !__builtin_strcmp(a.rows[i].key ? a.rows[i].key : "", b.rows[i].key ? b.rows[i].key : "")
                   && !__builtin_strcmp(a.rows[i].label ? a.rows[i].label : "", b.rows[i].label ? b.rows[i].label : "");

    body = !mapSame || !listSame || scanr::gpsState(a.gps)!=scanr::gpsState(b.gps);
}

// --- open decision #5: freeze the list order under a touch -------------------
// The policy (domr::orderLocated) is deterministic, so order never changes on
// its own. But signal strengths move, so between two touch-down frames the order
// *could* legitimately change -- and a reorder mid-gesture would slide a row out
// from under the finger. FrozenOrder snapshots the ordered key array at
// touch-down and keeps returning it until the finger lifts; only while no touch
// is active does it adopt a freshly computed order. Pure, host-tested.
class FrozenOrder {
public:
    static constexpr int MAXN = DomainStore::MAXROWS * 8;   // frozen keys headroom

    // Supply the CURRENT ordered key list and whether a touch is down. Returns
    // the keys the UI must bind rows to this frame; held steady across a touch.
    void update(const char* const* orderedKeys, int n, bool touchDown) {
        if (n > MAXN) n = MAXN;
        if (!touchDown) { snapshot_(orderedKeys, n); held_ = false; return; }
        if (!held_) { snapshot_(orderedKeys, n); held_ = true; }   // latch at touch-down
        // held: keep the existing snapshot regardless of a live reorder
    }
    int count() const { return n_; }
    const char* key(int i) const { return (i >= 0 && i < n_) ? keys_[i] : nullptr; }
    bool frozen() const { return held_; }

private:
    void snapshot_(const char* const* k, int n) {
        n_ = n; for (int i = 0; i < n; i++) keys_[i] = k ? k[i] : nullptr;
    }
    const char* keys_[MAXN] = {};
    int  n_ = 0;
    bool held_ = false;
};

// --- the adapter's pure sequencer (DomainScene twin of scanpres::Engine) ------
class DomainEngine {
public:
    void reset(int W, int H) {
        W_ = W; H_ = H;
        sched_.reset(H);
        trans_.leave();
        metrics_.clear();
        store_[0].begin(0, 0); store_[1].begin(0, 0);
        cur_ = 0; haveFront_ = false; forceFull_ = true;
    }
    void enter()        { trans_.enter(); forceFull_ = true; sched_.reset(H_); }
    void wake()         { trans_.enter(); forceFull_ = true; sched_.reset(H_); }
    void themeChanged() { trans_.enter(); forceFull_ = true; sched_.reset(H_); }
    void leave()        { trans_.leave(); sched_.reset(H_); }

    bool acceptsTouch() const { return trans_.acceptsTouch(); }
    bool pending()      const { return sched_.pending(); }
    bool idle()         const { return !sched_.pending(); }

    DomainStore&       back()  { return store_[cur_ ^ 1]; }
    const DomainScene& scene() const { return store_[cur_].scene(); }

    // Commit the filled back buffer: full on entry/wake/theme or first frame,
    // else only the changed bands; then swap it in. The one place that
    // invalidates, and only after the snapshot is populated.
    void commit() {
        const DomainScene& nw = store_[cur_ ^ 1].scene();
        if (forceFull_ || !haveFront_) {
            sched_.invalidateAll();
        } else {
            bool st = false, bd = false;
            sceneDirty(store_[cur_].scene(), nw, st, bd);
            const uieng::DomainLayout L = uieng::domain(W_, H_);
            const int statusBottom = L.statusCells[0].y + L.statusCells[0].h;
            const int navTop       = L.navCells[0].y;
            if (st) sched_.invalidate(0, statusBottom);
            if (bd) sched_.invalidate(statusBottom, navTop);
            { scanr::ScanScene za; za.rec = store_[cur_].scene().rec; scanr::ScanScene zb; zb.rec = nw.rec;  // design #1: STOP/REC lives on the nav bar now
              char ra[16], rb[16]; scanr::recLabel(za, ra, 16); scanr::recLabel(zb, rb, 16);
              if (__builtin_strcmp(ra, rb) || store_[cur_].scene().poi != nw.poi) sched_.invalidate(navTop, H_); }
        }
        cur_ ^= 1; haveFront_ = true; forceFull_ = false;
    }

    StripScheduler::Slice nextSlice(int budget) {
        const bool was = sched_.pending();
        const StripScheduler::Slice s = sched_.step(budget);
        if (was && s.done) { trans_.settled(); metrics_.frameDone(); }
        return s;
    }
    void recordSlice(uint32_t composeUs, uint32_t transferUs) { metrics_.slice(composeUs, transferUs); }
    void sampleCapture(uint32_t f, uint32_t u, uint32_t ble)  { metrics_.capture(f, u, ble); }
    const PresentMetrics& metrics() const { return metrics_; }
    void clearMetrics() { metrics_.clear(); }
    bool forcingFull() const { return forceFull_; }

private:
    DomainStore     store_[2];
    StripScheduler  sched_;
    Transition      trans_;
    PresentMetrics  metrics_;
    int  W_ = 0, H_ = 0, cur_ = 0;
    bool haveFront_ = false, forceFull_ = true;
};

}  // namespace dompres
