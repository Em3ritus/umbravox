// files_present.h -- the FILES device adapter's pure core. Shared machinery
// from scanpres::; FILES adds the FilesStore (owned row name/size strings +
// the confirm + status text), the status/body scene diff, and the FilesEngine.
#pragma once
#include "files_render.h"
#include "scan_present.h"
#include "model.h"
#include <cstdint>

namespace flspres {

using flsr::FilesScene;
using flsr::FileRowV;
using scanpres::StripScheduler;
using scanpres::Transition;
using scanpres::PresentMetrics;

class FilesStore {
public:
    static constexpr int MAXROWS = 4;                  // max rows a page shows
    static constexpr int NAMEMAX = ui::FILE_NAME_MAX;  // 24

    void begin(uint8_t theme, uint32_t now) {
        nr_ = 0; sc_ = FilesScene{}; sc_.theme = theme; sc_.now = now; sc_.rows = rows_; sc_.nRows = 0;
        statusBuf_[0] = 0; cName_[0] = 0; cSize_[0] = 0; wire_();
    }
    void gps(const flsr::GnssFix& f) { sc_.gps = f; }
    void header(bool bridge, bool pmu, bool charging) { sc_.bridgeOn=bridge; sc_.pmuOk=pmu; sc_.charging=charging; }
    void poi(ui::PoiStatus value) { sc_.poi=value; }
    void status(int nodes, bool hasCell, int batt, flsr::Rec rec) {
        sc_.nodes = nodes; sc_.hasCell = hasCell; sc_.battPct = batt; sc_.rec = rec;
    }
    void list(bool card, int total, int firstIndex, int sel, bool deletable) {
        sc_.card = card; sc_.total = total; sc_.firstIndex = firstIndex; sc_.sel = sel; sc_.deletable = deletable;
    }
    void bar(const char* text, uint16_t ink) { copy_(statusBuf_, 24, text); sc_.statusInk = ink; wire_(); }
    void modal(bool confirm, const char* name, const char* size) {
        sc_.confirm = confirm; copy_(cName_, NAMEMAX, name); copy_(cSize_, 12, size); wire_();
    }
    bool addRow(const char* name, const char* size, bool active) {
        if (nr_ >= MAXROWS) return false;
        copy_(nBuf_[nr_], NAMEMAX, name); copy_(sBuf_[nr_], 12, size);
        rows_[nr_] = FileRowV{ nBuf_[nr_], sBuf_[nr_], active };
        nr_++; sc_.nRows = nr_; return true;
    }
    const FilesScene& scene() const { return sc_; }
    int rowCount() const { return nr_; }

private:
    void wire_() { sc_.status = statusBuf_; sc_.confirmName = cName_; sc_.confirmSize = cSize_; }
    static void copy_(char* d, int cap, const char* src){ int k=0; for(;src&&src[k]&&k<cap-1;k++) d[k]=src[k]; d[k]=0; }
    FileRowV rows_[MAXROWS];
    char     nBuf_[MAXROWS][NAMEMAX], sBuf_[MAXROWS][12];
    char     statusBuf_[24], cName_[NAMEMAX], cSize_[12];
    int      nr_ = 0;
    FilesScene sc_;
};

inline void sceneDirty(const FilesScene& a, const FilesScene& b, bool& status, bool& body) {
    char la[16], lb[16];
    const bool gpsSame = scanr::gpsLabel(a.gps, la, 16) == scanr::gpsLabel(b.gps, lb, 16) && !__builtin_strcmp(la, lb);
    char ra[16], rb[16];
    scanr::ScanScene sa; sa.rec = a.rec; scanr::ScanScene sb; sb.rec = b.rec;
    const bool recSame = scanr::recLabel(sa, ra, 16) == scanr::recLabel(sb, rb, 16) && !__builtin_strcmp(ra, rb);
    status = !gpsSame || !recSame || a.nodes != b.nodes || a.hasCell != b.hasCell || a.battPct != b.battPct || a.bridgeOn != b.bridgeOn || a.pmuOk != b.pmuOk || a.charging != b.charging;

    bool same = a.card == b.card && a.total == b.total && a.firstIndex == b.firstIndex && a.sel == b.sel
        && a.deletable == b.deletable && a.confirm == b.confirm && a.statusInk == b.statusInk && a.nRows == b.nRows
        && !__builtin_strcmp(a.status?a.status:"", b.status?b.status:"")
        && !__builtin_strcmp(a.confirmName?a.confirmName:"", b.confirmName?b.confirmName:"");
    for (int i = 0; same && i < a.nRows; i++)
        same = a.rows[i].active == b.rows[i].active
               && !__builtin_strcmp(a.rows[i].name?a.rows[i].name:"", b.rows[i].name?b.rows[i].name:"")
               && !__builtin_strcmp(a.rows[i].size?a.rows[i].size:"", b.rows[i].size?b.rows[i].size:"");
    body = !same;
}

class FilesEngine {
public:
    void reset(int W, int H) {
        W_ = W; H_ = H; sched_.reset(H); trans_.leave(); metrics_.clear();
        store_[0].begin(0,0); store_[1].begin(0,0); cur_ = 0; haveFront_ = false; forceFull_ = true;
    }
    void enter() { trans_.enter(); forceFull_ = true; sched_.reset(H_); }
    void leave() { trans_.leave(); sched_.reset(H_); }
    bool acceptsTouch() const { return trans_.acceptsTouch(); }
    bool pending()      const { return sched_.pending(); }
    FilesStore&       back()  { return store_[cur_ ^ 1]; }
    const FilesScene& scene() const { return store_[cur_].scene(); }
    void commit() {
        const FilesScene& nw = store_[cur_ ^ 1].scene();
        if (forceFull_ || !haveFront_) sched_.invalidateAll();
        else { bool st=false,bd=false; sceneDirty(store_[cur_].scene(), nw, st, bd);
            const uieng::FilesLayout L = uieng::files(W_, H_);
            const int sb = L.statusCells[0].y + L.statusCells[0].h, nt = L.navCells[0].y;
            if (st) sched_.invalidate(0, sb);
            if (bd) sched_.invalidate(sb, nt);
            { scanr::ScanScene za; za.rec = store_[cur_].scene().rec; scanr::ScanScene zb; zb.rec = nw.rec;  // design #1: STOP/REC lives on the nav bar now
              char ra[16], rb[16]; scanr::recLabel(za, ra, 16); scanr::recLabel(zb, rb, 16);
              if (__builtin_strcmp(ra, rb) || store_[cur_].scene().poi != nw.poi) sched_.invalidate(nt, H_); } }
        cur_ ^= 1; haveFront_ = true; forceFull_ = false;
    }
    StripScheduler::Slice nextSlice(int budget) {
        const bool was = sched_.pending(); const StripScheduler::Slice s = sched_.step(budget);
        if (was && s.done) { trans_.settled(); metrics_.frameDone(); }
        return s;
    }
    const PresentMetrics& metrics() const { return metrics_; }
private:
    FilesStore store_[2]; StripScheduler sched_; Transition trans_; PresentMetrics metrics_;
    int W_ = 0, H_ = 0, cur_ = 0; bool haveFront_ = false, forceFull_ = true;
};

}  // namespace flspres
