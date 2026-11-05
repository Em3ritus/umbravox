// scan_present.h -- the SCAN device adapter's pure core.
//
// The compositor (scan_render.h) composes strips from a ScanScene; this is the
// logic that drives it on a real panel, kept free of hardware so it is
// host-testable (sim/test_scan_engine.cpp):
//
//   SceneStore   -- a STABLE snapshot of the scene. A redraw is sliced across
//                   many loop iterations, so the data and the contact key
//                   strings must not move under it; the store owns both.
//   StripScheduler -- resumable, bounded invalidation. A full repaint is
//                   delivered as a sequence of bounded row slices, never one
//                   blocking full-screen transfer.
//   Transition   -- entry/exit/wake/theme, and the touch gate that keeps a tap
//                   from dispatching against a half-transitioned screen.
//   PresentMetrics -- compose/transfer/max-slice/render-per-second and capture
//                   deltas, for the contract's later hardware comparison.
//
// Host and device compilable (no Arduino deps).
#pragma once
#include "scan_render.h"
#include <cstdint>
#include <cstddef>

namespace scanpres {

using scanr::FieldBlip;
using scanr::ScanScene;
using scanr::GnssFix;
using scanr::Rec;

// SAVING is feedback, not a second toggle. Remove only that action so a
// press during the drain cannot be deferred into a new recording session.
inline void recordingHits(uieng::HitList& hits, Rec rec) {
    if (rec != Rec::Saving) return;
    int n=0;
    for(int i=0;i<hits.count;++i)
        if(hits.items[i].action != uieng::Action::ToggleLogging) hits.items[n++]=hits.items[i];
    hits.count=n;
}

// --- stable scene snapshot ----------------------------------------------------
// Everything the scene points at lives here, so the scene handed to the
// compositor stays valid and unchanging for the whole (sliced) redraw even if
// the capture tables move on. Contacts are stored as PRE-PROJECTED blips
// (bearing/dist/band/age) -- the device's real field geometry, copied in, with
// no invented identity.
class SceneStore {
public:
    static constexpr int MAXC = 48;                   // == ui::MAX_CONTACTS
    static constexpr int NCH  = scanr::kChannels;     // 21

    // Open a new snapshot. Clears contacts and wires the scene to owned storage.
    void begin(uint8_t theme, uint32_t now) {
        nb_ = 0;
        sc_ = ScanScene{};
        sc_.theme = theme; sc_.now = now;
        sc_.blips = b_; sc_.nBlips = 0;
        sc_.channels = ch_; sc_.nChannels = NCH;
        for (int i = 0; i < NCH; i++) ch_[i] = 0;
        ch_buf_[0] = 0; band_buf_[0] = 0;
        sc_.ch = ch_buf_; sc_.chBand = band_buf_;
    }
    void bridge(bool on) { sc_.bridgeOn=on; }
    void animate(bool enabled) { sc_.animateSweep = enabled; }
    void sweep(uint8_t style) { sc_.sweep = style; }
    void video(const scanr::RadarVideo* v) { sc_.video = v; }   // a clip in the radar disc (wide panels)
    void fx(bool on) { sc_.fx.on = on; }                   // side-panel texture and motion (wide panels)
    scanr::Fx& fxState() { return sc_.fx; }                // the engine fills the history in just before a commit
    void alert(const scanr::AlertScene& a) { sc_.alert = a; }
    void gps(const GnssFix& f)                    { sc_.gps = f; }
    void storage(ui::StorageState sd){sc_.sd=sd;}
    void header(bool bridge, bool pmu, bool charging) { sc_.bridgeOn=bridge; sc_.pmuOk=pmu; sc_.charging=charging; }
    void poi(ui::PoiStatus value) { sc_.poi=value; }
    void status(int nodes, bool hasCell, int batt, Rec rec) {
        sc_.nodes = nodes; sc_.hasCell = hasCell; sc_.battPct = batt; sc_.rec = rec;
    }
    void counts(long wifi, long g5, long ble)     { sc_.wifi = wifi; sc_.g5 = g5; sc_.ble = ble; }
    void channel(const char* ch, const char* band) {
        copy_(ch_buf_,   sizeof ch_buf_,   ch);
        copy_(band_buf_, sizeof band_buf_, band);
        sc_.ch = ch_buf_; sc_.chBand = band_buf_;
    }
    void setChannel(int i, int load)              { if ((unsigned)i < (unsigned)NCH) ch_[i] = load; }

    // Append a pre-projected contact (copied in). Returns false when full.
    bool addBlip(uint16_t bearing, uint8_t dist, uint8_t band, uint32_t lastSeen) {
        if (nb_ >= MAXC) return false;
        b_[nb_] = FieldBlip{ bearing, dist, band, lastSeen };
        nb_++; sc_.nBlips = nb_;
        return true;
    }

    const ScanScene& scene() const { return sc_; }
    int contactCount() const { return nb_; }

private:
    static void copy_(char* dst, int cap, const char* src) {
        int k = 0; for (; src && src[k] && k < cap - 1; k++) dst[k] = src[k]; dst[k] = 0;
    }
    FieldBlip b_[MAXC];
    int       nb_ = 0;
    int       ch_[NCH];
    char      ch_buf_[12], band_buf_[12];
    ScanScene sc_;
};

// The field radius the compositor draws markers within, from the layout (same
// derivation as composeStrip), so the diff can project markers exactly.
inline int fieldRadius(int W, int H) {
    return (scanr::radarBounds(W,H).w - 5) / 2;
}

// --- side-panel history: peak hold and the per-second sample -------------------------------------------------
// The engine owns the only copy that moves. Each commit it advances by the animation ticks that have passed since
// the last one (the peaks fall, a sample is taken every kSparkTicks) and copies the result into the snapshot, so a
// scene is stable for its whole sliced redraw. Pure, host-tested (sim/test_scan_fx.cpp).
class FxTracker {
public:
    static constexpr int kDecay = 6;                              // peak fall per tick: 100 -> 0 in ~8 s
    void reset() { init_ = false; }
    void advance(const ScanScene& sc, scanr::Fx& out) {
        const long c[3] = { sc.wifi, sc.g5, sc.ble };
        int ch[scanr::kChannels];
        for (int i = 0; i < scanr::kChannels; i++) ch[i] = sc.channels && sc.nChannels == scanr::kChannels ? sc.channels[i] : 0;
        advance(sc.now, c, ch, out);
    }
    void advance(uint32_t now, const long* counts, const int* load, scanr::Fx& out) {
        const uint32_t tick = now / scanr::kFxMs;
        const int32_t d = init_ ? (int32_t)(tick - tick_) : 0;
        if (!init_ || d < 0 || d > scanr::kSparkTicks * scanr::kSpark) {   // first sample, or the clock jumped (sleep, resync): start over
            init_ = true; tick_ = tick; n_ = tick;
            for (int k = 0; k < 3; k++) { last_[k] = counts[k]; for (int j = 0; j < scanr::kSpark; j++) spark_[k][j] = 0; }
            for (int i = 0; i < scanr::kChannels; i++) peak_[i] = clamp_(load[i]);
        } else {
            for (int s = 0; s < d; s++) {
                n_++;
                for (int i = 0; i < scanr::kChannels; i++) {
                    const int cur = clamp_(load[i]), fall = peak_[i] > kDecay ? peak_[i] - kDecay : 0;
                    peak_[i] = (uint8_t)(cur > fall ? cur : fall);
                }
                if (n_ % scanr::kSparkTicks == 0) {
                    for (int k = 0; k < 3; k++) {
                        long dv = counts[k] - last_[k]; last_[k] = counts[k];
                        if (dv < 0) dv = 0; if (dv > 255) dv = 255;
                        for (int j = 0; j + 1 < scanr::kSpark; j++) spark_[k][j] = spark_[k][j + 1];
                        spark_[k][scanr::kSpark - 1] = (uint8_t)dv;
                    }
                }
            }
            tick_ = tick;
            for (int i = 0; i < scanr::kChannels; i++) { const int cur = clamp_(load[i]); if (cur > peak_[i]) peak_[i] = (uint8_t)cur; }   // a rise shows at once
        }
        out.on = true;
        for (int k = 0; k < 3; k++) for (int j = 0; j < scanr::kSpark; j++) out.spark[k][j] = spark_[k][j];
        for (int i = 0; i < scanr::kChannels; i++) out.peak[i] = peak_[i];
    }
private:
    static int clamp_(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }
    bool init_ = false; uint32_t tick_ = 0, n_ = 0;
    long last_[3] = {0, 0, 0};
    uint8_t spark_[3][scanr::kSpark] = {}, peak_[scanr::kChannels] = {};
};

// --- scene diff: which display bands changed ---------------------------------
// Compares two stable scenes by what the panel would SHOW. `status` is the top
// chrome (GPS/mesh/batt/log); `body` is the count tiles + hero + spectrum; the
// nav bar is static and never reported. The body compares the RENDERED marker
// state (markers projected with each scene's own clock), so a contact that
// fades or expires over time repaints even with no new packets, while a clock
// tick that changes nothing visible does not. Pure.
// Which parts of the scene changed (what the panel would SHOW): the header chrome, each count tile, the
// channel readout, the spectrum, the contacts, and the sweep. Pure.
struct ScanParts { bool status = false, count[3] = {false, false, false}, ch = false, chan = false, marks = false, sweep = false, alert = false;
                   bool fxCard[3] = {false, false, false}, fxSpec = false; };   // side-panel texture and motion moved
inline ScanParts sceneParts(int W, int H, const ScanScene& a, const ScanScene& b) {
    ScanParts p;
    char la[16], lb[16];
    const bool gpsSame = scanr::gpsLabel(a.gps, la, 16) == scanr::gpsLabel(b.gps, lb, 16)
                         && !__builtin_strcmp(la, lb);
    char ra[16], rb[16];
    const bool recSame = scanr::recLabel(a, ra, 16) == scanr::recLabel(b, rb, 16)
                         && !__builtin_strcmp(ra, rb);
    p.status = !gpsSame || !recSame || a.nodes != b.nodes
             || a.hasCell != b.hasCell || a.battPct != b.battPct || a.bridgeOn != b.bridgeOn || a.pmuOk != b.pmuOk || a.charging != b.charging || a.sd != b.sd;
    p.count[0] = a.wifi != b.wifi; p.count[1] = a.g5 != b.g5; p.count[2] = a.ble != b.ble;
    p.ch = __builtin_strcmp(a.ch ? a.ch : "", b.ch ? b.ch : "") || __builtin_strcmp(a.chBand ? a.chBand : "", b.chBand ? b.chBand : "");
    bool chanSame = a.nChannels == b.nChannels;
    for (int i = 0; chanSame && i < a.nChannels; i++) chanSame = a.channels[i] == b.channels[i];
    p.chan = !chanSame;
    const int fr = fieldRadius(W, H);
    bool markSame = a.nBlips == b.nBlips;
    for (int i = 0; markSame && i < a.nBlips; i++) {
        const scanr::Marker ma = scanr::placeMarker(a.blips[i].bearing, a.blips[i].dist,
                                     a.blips[i].band, a.now - a.blips[i].lastSeen, fr);
        const scanr::Marker mb = scanr::placeMarker(b.blips[i].bearing, b.blips[i].dist,
                                     b.blips[i].band, b.now - b.blips[i].lastSeen, fr);
        markSame = ma.shown == mb.shown
                   && (!ma.shown || (ma.dx == mb.dx && ma.dy == mb.dy
                                     && ma.sizePx == mb.sizePx && ma.band == mb.band));
    }
    const bool vidSame = a.video == b.video && (!b.video || scanr::videoFrame(*a.video, a.now) == scanr::videoFrame(*b.video, b.now));
    p.marks = !markSame || (W >= 400 && !vidSame);      // a new clip frame changes the whole disc, not just a wedge
    if (W >= 400 && a.fx.on && b.fx.on) {                              // wide panels: the tick, the sparklines and the peaks
        const bool tick = a.now / scanr::kFxMs != b.now / scanr::kFxMs;
        for (int k = 0; k < 3; k++) p.fxCard[k] = tick || __builtin_memcmp(a.fx.spark[k], b.fx.spark[k], sizeof a.fx.spark[k]) != 0;
        p.fxSpec = tick || __builtin_memcmp(a.fx.peak, b.fx.peak, sizeof a.fx.peak) != 0;
    } else if (W >= 400 && a.fx.on != b.fx.on) { for (int k = 0; k < 3; k++) p.fxCard[k] = true; p.fxSpec = true; }
    p.alert = W >= 400 && (a.alert.active != b.alert.active
              || (b.alert.active && (a.alert.age != b.alert.age || a.alert.conf != b.alert.conf || a.alert.threat != b.alert.threat || a.alert.style != b.alert.style
                                     || __builtin_strcmp(a.alert.label, b.alert.label) || __builtin_strcmp(a.alert.name, b.alert.name) || __builtin_strcmp(a.alert.why, b.alert.why))));
    const uint8_t sa = a.animateSweep ? scanr::liveStyle(a.sweep, W) : uint8_t(ui::SW_RADAR), sb = b.animateSweep ? scanr::liveStyle(b.sweep, W) : uint8_t(ui::SW_RADAR);
    p.sweep = !(a.animateSweep == b.animateSweep && sa == sb &&
        (!b.animateSweep || (sb == ui::SW_RADAR ? scanr::sweepAngle(a.now) == scanr::sweepAngle(b.now) : a.now == b.now)));
    return p;
}
// The same alert (same threat, level, rite and text), only its age differs.
inline bool sameAlert(const scanr::AlertScene& a, const scanr::AlertScene& b) {
    return a.active && b.active && a.threat == b.threat && a.conf == b.conf && a.style == b.style
        && !__builtin_strcmp(a.label, b.label) && !__builtin_strcmp(a.name, b.name) && !__builtin_strcmp(a.why, b.why);
}
inline void sceneDirty(int W, int H, const ScanScene& a, const ScanScene& b,
                       bool& status, bool& body, bool* fieldOnly = nullptr, bool* sweepOnly = nullptr) {
    const ScanParts p = sceneParts(W, H, a, b);
    status = p.status;
    const bool counts = p.count[0] || p.count[1] || p.count[2];
    body = counts || p.ch || p.chan || p.marks || p.sweep || p.alert;
    if (fieldOnly) *fieldOnly = body && !counts && !p.ch && !p.chan && !p.alert;      // an alert covers the hero, not just the radar columns
    if (sweepOnly) *sweepOnly = body && !counts && !p.ch && !p.chan && !p.marks && !p.alert;   // nothing but the sweep moved
}

// --- resumable, bounded strip scheduler --------------------------------------
// Dirty rows are unioned into one strip-aligned span; step() hands out at most
// `maxStrips` strips of it at a time, so a full invalidation is delivered over
// several bounded slices rather than one blocking transfer. The device renders
// the returned [y0,y1) with Compositor::renderRows and pushes it to the panel.
class StripScheduler {
public:
    static constexpr int STRIP = scanr::Compositor::STRIP_H;   // 16

    struct Slice { int y0, y1; bool done; };   // render rows [y0,y1); done = frame complete

    void reset(int H) { H_ = H; cur_ = end_ = 0; active_ = false; }

    void invalidate(int y0, int y1) {
        if (y1 <= y0) return;
        int a = (y0 / STRIP) * STRIP;
        int b = ((y1 + STRIP - 1) / STRIP) * STRIP;
        if (a < 0) a = 0; if (b > H_) b = H_;
        if (!active_) { cur_ = a; end_ = b; active_ = true; }
        else { if (a < cur_) cur_ = a; if (b > end_) end_ = b; }   // union; may overpaint
    }
    void invalidateAll() { invalidate(0, H_); }

    bool pending() const { return active_ && cur_ < end_; }

    // Advance by at most maxStrips strips. When nothing is pending, returns an
    // empty, done slice.
    Slice step(int maxStrips) {
        if (!pending()) { active_ = false; return Slice{ cur_, cur_, true }; }
        if (maxStrips < 1) maxStrips = 1;
        int y1 = cur_ + maxStrips * STRIP;
        if (y1 > end_) y1 = end_;
        Slice s{ cur_, y1, false };
        cur_ = y1;
        s.done = cur_ >= end_;
        if (s.done) active_ = false;
        return s;
    }

private:
    int  H_ = 0, cur_ = 0, end_ = 0;
    bool active_ = false;
};

// --- transition state + touch gate -------------------------------------------
// Entering (fresh entry, wake, or a theme change) forces a full repaint and
// swallows touches until that repaint has finished -- a tap must never
// dispatch against a screen that is only half drawn.
class Transition {
public:
    enum class Phase : uint8_t { Idle, Entering, Steady };

    void enter()  { phase_ = Phase::Entering; }   // entry / wake / theme invalidation
    void leave()  { phase_ = Phase::Idle; }       // left the SCAN screen
    void settled() { if (phase_ == Phase::Entering) phase_ = Phase::Steady; }  // entry frame complete

    bool needsFull()    const { return phase_ == Phase::Entering; }
    bool acceptsTouch() const { return phase_ == Phase::Steady; }
    Phase phase() const { return phase_; }

private:
    Phase phase_ = Phase::Idle;
};

// --- native-touch edge gate ---------------------------------------------------
// A down-edge fires once per press and only while dispatch is accepted. A press
// that begins gated (during a transition) is CONSUMED, so a finger held across
// completion does not fire a spurious tap; reset() consumes any in-progress
// press (on entry/exit) to stop a held touch clicking through. Pure.
class TouchGate {
public:
    void reset() { armed_ = false; }                   // consume the current press, if any
    bool edge(bool contact, bool accepts) {
        if (!contact) { armed_ = true; return false; } // released -> re-arm
        if (!armed_)  return false;                    // held across a transition (reset), or already fired
        // A NEW press (armed) that arrives while dispatch is NOT accepted (a
        // screen entry in progress) must not be silently eaten: DEFER it --
        // leave it armed so it fires the moment entry completes. Only the
        // originating press that navigated in stays unarmed (reset above), so
        // the click-through protection still holds.
        if (!accepts) return false;                    // defer, keep armed_
        armed_ = false;                                // accepted -> consume and fire exactly once
        return true;
    }
    bool armed() const { return armed_; }
private:
    bool armed_ = true;
};

// --- instrumentation ----------------------------------------------------------
// One window's worth of timings and capture deltas; the device samples into it
// and rolls it up per interval for /perf or a console line.
struct PresentMetrics {
    uint32_t composeUs = 0, transferUs = 0, renderUs = 0;   // sums over the window
    uint32_t diffUs = 0, entryMaxMs = 0;
    uint32_t maxSliceUs = 0;                                 // worst single slice
    uint32_t slices = 0, frames = 0;
    uint32_t capFrames = 0, capUniq = 0, bleReports = 0;     // ACCUMULATED deltas this window

    void slice(uint32_t composeUs_, uint32_t transferUs_) {
        work(composeUs_, 0, transferUs_);
    }
    void work(uint32_t composeUs_, uint32_t diffUs_, uint32_t transferUs_) {
        composeUs += composeUs_; diffUs += diffUs_; transferUs += transferUs_;
        const uint32_t s = composeUs_ + diffUs_ + transferUs_;
        renderUs += s; slices++;
        if (s > maxSliceUs) maxSliceUs = s;
    }
    void frameDone() { frames++; }

    // Step a running counter since the last sample, tolerant of a source reset
    // (goes backwards without a true 2^32 wrap). Matches perflog::delta.
    static uint32_t delta(uint32_t cur, uint32_t& last) {
        const uint32_t prev = last; last = cur;
        if (cur >= prev) return cur - prev;
        if (prev - cur > 0x80000000u) return cur - prev;   // wrapped
        return cur;                                         // reset
    }
    // Accumulate per-loop capture deltas into the window. The first sample
    // primes the baseline and adds nothing (otherwise it would count the whole
    // running total once); clear() keeps the baseline, so the next window
    // continues seamlessly. BLE reports are carried for the capture comparison.
    void capture(uint32_t frames_, uint32_t uniq_, uint32_t bleReports_) {
        if (!primed_) { capFramesLast_ = frames_; capUniqLast_ = uniq_; bleLast_ = bleReports_;
                        primed_ = true; return; }
        capFrames  += delta(frames_,     capFramesLast_);
        capUniq    += delta(uniq_,       capUniqLast_);
        bleReports += delta(bleReports_, bleLast_);
    }

    // Render time as parts-per-thousand of the interval: us of render over ms
    // of wall clock is already per-mille (e.g. 120 == 12.0% of wall time spent
    // composing+transferring).
    uint32_t renderPerMille(uint32_t intervalMs) const {
        if (!intervalMs) return 0;
        return (uint32_t)((uint64_t)renderUs / intervalMs);
    }
    // End of a reporting window: zero the window sums but KEEP the capture
    // baseline + primed state so cross-window deltas stay continuous.
    void clear() {
        composeUs = diffUs = transferUs = renderUs = maxSliceUs = entryMaxMs = 0;
        slices = frames = capFrames = capUniq = bleReports = 0;
    }

private:
    uint32_t capFramesLast_ = 0, capUniqLast_ = 0, bleLast_ = 0;
    bool     primed_ = false;
};

// --- the adapter's pure sequencer --------------------------------------------
// Owns the double-buffered snapshot, the scheduler, the transition gate and the
// metrics, so the whole entry->commit->service flow is host-testable; only the
// panel sink and the data sources are device-side. The invariant that drove
// finding 1: a snapshot is committed (which is what invalidates) BEFORE any
// slice is ever served, so entry never queues a default/stale frame.
class Engine {
public:
    void reset(int W, int H) {
        W_ = W; H_ = H;
        sched_.reset(H);
        trans_.leave();
        metrics_.clear();
        store_[0].begin(0, 0); store_[1].begin(0, 0);   // valid (empty) scenes, not stale
        cur_ = 0; haveFront_ = false; forceFull_ = true;
    }
    // Entry/wake/theme discard any partially serviced frame (its pending slices
    // are stale) and force a full repaint; the commit that follows paints the
    // CURRENT scene. leave() likewise drops the pending frame so a stale one can
    // never resume on re-entry.
    void enter()        { fx_.reset(); trans_.enter(); forceFull_ = true; win_ = false; nreg_ = 0; sched_.reset(H_); }
    void wake()         { fx_.reset(); trans_.enter(); forceFull_ = true; win_ = false; nreg_ = 0; sched_.reset(H_); }
    void themeChanged() { fx_.reset(); trans_.enter(); forceFull_ = true; win_ = false; nreg_ = 0; sched_.reset(H_); }
    void leave()        { fx_.reset(); trans_.leave(); win_ = false; nreg_ = 0; sched_.reset(H_); }

    // True when the pending frame changed ONLY the radar (a sweep step): the caller may compose and stage
    // just the columns [lo,hi) of the returned rows instead of the full width.
    bool window(int& lo, int& hi) const { lo = wlo_; hi = whi_; return win_; }

    // Wide panels: the pending frame's dirty REGIONS (count tiles, channel readout, spectrum, radar or its
    // wedge, header and nav bands). Returns 0 when the frame is a plain full-width one (entry, theme change,
    // compact panels, or a commit while a frame was still pending); then rows come from the scheduler alone.
    static constexpr int kMaxRegions = 12;
    int regions(uieng::Rect* out, int cap) const {
        const int n = nreg_ < cap ? nreg_ : cap;
        for (int i = 0; i < n; i++) out[i] = reg_[i];
        return n;
    }

    bool acceptsTouch() const { return trans_.acceptsTouch(); }
    bool pending()      const { return sched_.pending(); }
    bool idle()         const { return !sched_.pending(); }   // ready for a new snapshot

    SceneStore&      back()  { return store_[cur_ ^ 1]; }     // caller fills this
    const ScanScene& scene() const { return store_[cur_].scene(); }  // served buffer

    // Commit the filled back buffer: decide dirty (full on entry/wake/theme or
    // first frame, else only the changed bands), invalidate, then swap it in.
    // This is the one place that invalidates, and it runs only after the
    // snapshot is populated -- so a slice is never served against a stale scene.
    void commit() {
        const ScanScene& nw = store_[cur_ ^ 1].scene();
        if (W_ >= 400 && nw.fx.on) fx_.advance(nw, store_[cur_ ^ 1].fxState());   // history first: the diff below compares it
        const bool wasPending = sched_.pending(), wasWin = win_;
        win_ = false; nreg_ = 0;
        const int lvOld = W_ >= 400 ? scanr::alertLevel(store_[cur_].scene().alert) : 0, lvNew = W_ >= 400 ? scanr::alertLevel(nw.alert) : 0;
        if (forceFull_ || !haveFront_) {
            sched_.invalidateAll();
        } else if (lvOld == 2 || lvNew == 2) {
            const ScanScene& pa = store_[cur_].scene();
            if (lvOld == 2 && lvNew == 2 && !wasPending && sameAlert(pa.alert, nw.alert) && scanr::alertSettled(pa.alert) && scanr::alertSettled(nw.alert)) {
                uieng::Rect rg[8]; const int n = scanr::alertRegions({0, 0, W_, H_}, nw.alert, nw.theme, rg, 8);      // settled: only the rite square and border move
                for (int i = 0; i < n; i++) { reg_[nreg_++] = rg[i]; sched_.invalidate(rg[i].y, rg[i].y + rg[i].h); }
            } else sched_.invalidateAll();             // a HIGH alert owns the whole screen, header and nav included: repaint it all
        } else if (W_ >= 400 && !wasPending) {
            // Wide panel: dirty REGIONS. Each changed part of the scene repaints only its own rectangle.
            const ScanScene& pv = store_[cur_].scene();
            const ScanParts p = sceneParts(W_, H_, pv, nw);
            const uieng::ScanLayout L = uieng::scan(W_, H_);
            const int statusBottom = L.statusCells[0].y + L.statusCells[0].h, navTop = L.navCells[0].y;
            auto add = [&](const uieng::Rect& r) {
                if (r.w <= 0 || r.h <= 0) return;
                if (nreg_ < kMaxRegions) reg_[nreg_++] = r;
                else {                                                       // out of slots: grow the last region over it (never drop a repaint)
                    uieng::Rect& l = reg_[nreg_ - 1];
                    const int x0 = std::min(l.x, r.x), y0 = std::min(l.y, r.y), x1 = std::max(l.x + l.w, r.x + r.w), y1 = std::max(l.y + l.h, r.y + r.h);
                    l = { x0, y0, x1 - x0, y1 - y0 }; sched_.invalidate(y0, y1);
                }
                sched_.invalidate(r.y, r.y + r.h);
            };
            if (p.alert) {                             // a LOW/MED alert on, off or animating: the hero area, or once settled only what still moves
                if (lvOld == 1 && lvNew == 1 && sameAlert(pv.alert, nw.alert) && scanr::alertSettled(pv.alert) && scanr::alertSettled(nw.alert)) {
                    uieng::Rect rg[8]; const int n = scanr::alertRegions(L.hero, nw.alert, nw.theme, rg, 8);
                    for (int i = 0; i < n; i++) add(rg[i]);
                } else add(L.hero);
            }
            if (p.status) add({0, 0, W_, statusBottom});
            for (int i = 0; i < 3; i++) if (p.count[i] || p.fxCard[i]) add(L.tiles[i]);
            const bool covered = lvOld == 1 && lvNew == 1;           // a LOW/MED alert hides the hero, so its radar, channel readout and spectrum need no repaint
            if (p.ch && !covered)   add(scanr::chRect(W_, H_));
            if ((p.chan || p.fxSpec) && !covered) add(scanr::specRect(W_, H_));
            if ((p.marks || p.sweep) && !covered) {
                const auto radar = scanr::radarBounds(W_, H_);
                uieng::Rect boxes[6]; int nb = 0;
                if (!p.marks && pv.animateSweep && nw.animateSweep) {      // sweep only: just what moved (old and new wedge, arc, ring band...)
                    const uint8_t sv = scanr::liveStyle(pv.sweep, W_), sn = scanr::liveStyle(nw.sweep, W_);
                    if (sv == sn) nb = scanr::sweepRects(sn, radar, pv.now, nw.now, boxes, 6);
                }
                if (!nb) { boxes[0] = radar; nb = 1; }
                for (int i = 0; i < nb; i++) add(boxes[i]);
            }
            char ra[16], rb[16];
            scanr::recLabel(pv, ra, 16); scanr::recLabel(nw, rb, 16);
            if (__builtin_strcmp(ra, rb) || pv.poi != nw.poi) add({0, navTop, W_, H_ - navTop});   // REC/STOP and POI live on the nav
        } else {
            bool st = false, bd = false, fieldOnly = false, sweepOnly = false;
            sceneDirty(W_, H_, store_[cur_].scene(), nw, st, bd, &fieldOnly, &sweepOnly);
            // Sweep and nothing else: redraw only where the old and new wedge are, not the whole radar.
            const auto radar = scanr::radarBounds(W_, H_);
            uieng::Rect box = radar;
            const ScanScene& pv = store_[cur_].scene();
            if (sweepOnly && !wasPending && pv.animateSweep && nw.animateSweep) {
                const uint8_t sv = scanr::liveStyle(pv.sweep, W_), sn = scanr::liveStyle(nw.sweep, W_);
                box = sv == sn ? scanr::sweepBox(sn, radar, pv.now, nw.now) : radar;
            }
            const uieng::ScanLayout L = uieng::scan(W_, H_);
            const int statusBottom = L.statusCells[0].y + L.statusCells[0].h;
            const int navTop       = L.navCells[0].y;
            if (st) sched_.invalidate(0, statusBottom);
            if (bd) {
                if (fieldOnly) sched_.invalidate(box.y, box.y + box.h);
                else sched_.invalidate(statusBottom, navTop);
            }
            // design #1 SCAN carries the REC/STOP control on the NAV bar, which
            // is otherwise static: repaint the nav band only when the recording
            // state actually changes (not on every GPS/batt status tick).
            char ra[16], rb[16];
            scanr::recLabel(store_[cur_].scene(), ra, 16);
            scanr::recLabel(nw, rb, 16);
            const bool navDirty = __builtin_strcmp(ra, rb) || store_[cur_].scene().poi != nw.poi;
            if (navDirty) sched_.invalidate(navTop, H_);
            if (!st && bd && fieldOnly && !navDirty && (!wasPending || wasWin)) {   // radar only: narrow the redraw to its columns
                win_ = true; wlo_ = box.x < 0 ? 0 : box.x; whi_ = box.x + box.w > W_ ? W_ : box.x + box.w;
            }
        }
        cur_ ^= 1; haveFront_ = true; forceFull_ = false;
    }

    // One bounded step of the current frame. On completion the transition
    // settles (touch re-enabled) and the frame is counted.
    StripScheduler::Slice nextSlice(int budget) {
        const bool was = sched_.pending();
        const StripScheduler::Slice s = sched_.step(budget);
        if (was && s.done) { trans_.settled(); metrics_.frameDone(); }   // a real frame finished
        return s;
    }
    void recordSlice(uint32_t composeUs, uint32_t transferUs) { metrics_.slice(composeUs, transferUs); }
    void sampleCapture(uint32_t f, uint32_t u, uint32_t ble)  { metrics_.capture(f, u, ble); }
    const PresentMetrics& metrics() const { return metrics_; }
    void clearMetrics() { metrics_.clear(); }

    bool forcingFull() const { return forceFull_; }   // for tests

private:
    SceneStore       store_[2];
    FxTracker        fx_;
    StripScheduler   sched_;
    Transition       trans_;
    PresentMetrics   metrics_;
    int  W_ = 0, H_ = 0, cur_ = 0;
    bool win_ = false; int wlo_ = 0, whi_ = 0;
    uieng::Rect reg_[kMaxRegions]; int nreg_ = 0;
    bool haveFront_ = false, forceFull_ = true;
};

// --- idle-suspend drain policy ------------------------------------------------
// A full repaint is sliced, so the pass that queues one (e.g. an external paint
// on animation exit while the panel is still dark) services only its first
// slice; the loop then idle-suspends and would early-return before the rest.
// The scheduler's own pending() IS the latched obligation -- it persists across
// passes -- so the policy is simply: while suspended, keep servicing a pending
// redraw to completion, but NEVER while the low-power animation owns the panel
// (it draws its own pixels), and never START a new redraw asleep (the caller
// only ever calls service(), not snapshot(), on this path). Once pending clears,
// the caller resumes quiet suspension. Pure, host-tested.
inline bool serviceWhileSuspended(bool pending, bool animOn) {
    return pending && !animOn;
}

}  // namespace scanpres
