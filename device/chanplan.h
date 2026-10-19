// Who covers which channels, as a function of how many radios there are.
//
// Header-only and free of hardware, so sim/test_chanplan.cpp can print the
// plan for one through eight nodes without owning eight boards.
#pragma once
#include <stdint.h>
#include "mesh.h"
#include "model.h"

namespace chanplan {

struct Plan { uint8_t n; uint8_t ch[mesh::MAX_ASSIGN]; bool camp; };

// --- channel allocation --------------------------------------------------
//
// Two facts drive this.
//
// Camping beats sweeping, but only on channels worth camping. A radio parked
// on channel 6 hears every beacon on it; a radio sweeping twenty channels
// hears roughly one in twenty. For *discovery* sweeping is fine -- one
// beacon is enough to log an AP -- but for *tracking* an AP's signal over
// time, which is what the detail chart shows, camping is far better.
//
// Band changes are not free. Switching bands means a set_band as well as a
// set_channel, so a node whose list straddles 2.4 and 5GHz pays that on
// every wrap. Grouping a node's channels into one band avoids it.
//
// So: spend the first nodes on breadth, and only start dedicating radios to
// the busy 2.4GHz channels once there are enough that breadth is covered.
inline constexpr uint8_t SOCIAL[] = {6, 11};     // the channels worth a whole radio

// Largest channel set planCluster will take. A coordinator's slice is a subset
// of the band (13 + 25 channels at most), so this is generous.
inline constexpr int MAX_POOL = 64;

// Fill `plans` for `nNodes` nodes sharing the `nCh` channels in `channels`.
//
// The pool is whatever the caller hands over: planChannels passes the whole
// band, a coordinator passes only its own slice. Nothing outside `channels`
// is ever assigned. 5GHz is anything above channel 14.
//
// Only SOCIAL channels that are actually in the pool can be camped, and a
// camper is only spent when there are enough radios (see below). Whatever is
// left is swept, split by band in proportion to how many channels each has.
inline void planCluster(const uint8_t* channels, int nCh, int nNodes, Plan* plans) {
    for (int i = 0; i < nNodes; i++) { plans[i].n = 0; plans[i].camp = false; }
    if (nNodes <= 0 || nCh <= 0) return;
    if (nCh > MAX_POOL) nCh = MAX_POOL;

    // Dedicated campers only once there are three or more radios: with one
    // or two, every channel spent camping is breadth we cannot spare.
    const int wantCamp = nNodes >= 4 ? 2 : nNodes == 3 ? 1 : 0;

    // The first `wantCamp` SOCIAL channels that this pool actually holds.
    uint8_t camped[2];
    int campers = 0;
    for (int s = 0; s < 2 && campers < wantCamp; s++) {
        bool present = false;
        for (int i = 0; i < nCh; i++) if (channels[i] == SOCIAL[s]) present = true;
        if (present) camped[campers++] = SOCIAL[s];
    }
    for (int i = 0; i < campers; i++) {
        plans[i].n = 1;
        plans[i].ch[0] = camped[i];
        plans[i].camp = true;
    }

    // What is left to sweep, grouped by band. Only the camped SOCIAL channels
    // come out -- taking out both when only one is camped leaves the other
    // covered by nobody, which is exactly what it once did.
    uint8_t l24[MAX_POOL], l5[MAX_POOL];
    int n24 = 0, n5 = 0;
    for (int i = 0; i < nCh; i++) {
        const uint8_t ch = channels[i];
        bool isCamped = false;
        for (int c = 0; c < campers; c++) if (ch == camped[c]) isCamped = true;
        if (isCamped) continue;
        if (ch <= 14) l24[n24++] = ch; else l5[n5++] = ch;
    }

    const int sweepers = nNodes - campers;
    if (sweepers <= 0 || n24 + n5 == 0) return;

    if (sweepers == 1) {
        // One sweeper takes everything, band change per wrap and all --
        // there is no second radio to split with.
        Plan& p = plans[campers];
        for (int i = 0; i < n24 && p.n < mesh::MAX_ASSIGN; i++) p.ch[p.n++] = l24[i];
        for (int i = 0; i < n5  && p.n < mesh::MAX_ASSIGN; i++) p.ch[p.n++] = l5[i];
        return;
    }

    // Two or more: split by band, in proportion to how many channels each
    // band has, with at least one radio on each so neither goes unheard --
    // unless the pool only has one band, in which case it gets them all.
    int s24;
    if (n5 == 0)       s24 = sweepers;
    else if (n24 == 0) s24 = 0;
    else {
        s24 = (sweepers * n24 + (n24 + n5) / 2) / (n24 + n5);
        if (s24 < 1) s24 = 1;
        if (s24 > sweepers - 1) s24 = sweepers - 1;
    }
    const int s5 = sweepers - s24;

    int slot = campers;
    for (int k = 0; k < s24; k++, slot++) {
        const int lo = n24 * k / s24, hi = n24 * (k + 1) / s24;
        Plan& p = plans[slot];
        for (int i = lo; i < hi && p.n < mesh::MAX_ASSIGN; i++) p.ch[p.n++] = l24[i];
    }
    for (int k = 0; k < s5; k++, slot++) {
        const int lo = n5 * k / s5, hi = n5 * (k + 1) / s5;
        Plan& p = plans[slot];
        for (int i = lo; i < hi && p.n < mesh::MAX_ASSIGN; i++) p.ch[p.n++] = l5[i];
    }
}

// Fill `plans` for `live` nodes over the whole band, less the channel the
// Core holds for reporting.
inline void planChannels(int live, Plan* plans) {
    uint8_t all[ui::CH24 + ui::CH5];
    int n = 0;
    for (int i = 0; i < ui::CH24; i++) if (ui::CHAN24[i] != mesh::REPORT_CHAN) all[n++] = ui::CHAN24[i];
    for (int i = 0; i < ui::CH5; i++) all[n++] = ui::CHAN5[i];
    planCluster(all, n, live, plans);
}

// --- band partitioning across coordinators --------------------------------
//
// With N coordinators the CYD splits the band into disjoint slices, one per
// coordinator, each with its own report channel. A coordinator plans across
// its slice (planCluster) and reports on its report channel; the CYD collects
// on REPORT_CHAN, which is held out of every slice.
struct Slice { uint8_t nChan; uint8_t chans[mesh::MAX_SLICE]; uint8_t reportChan; };
static_assert(ui::CH24 - 1 + ui::CH5 <= mesh::MAX_SLICE,
              "one-coordinator slice must fit MAX_SLICE");

// Fill `out` with one slice per coordinator and return how many were produced.
// One coordinator takes the whole band. Two or more split it: slice 0 is 2.4GHz
// less the collection channel and reports on SOCIAL[0]; slice 1 is all 5GHz and
// reports on CHAN5[0]. The slices are disjoint and their report channels differ.
// Produces at most 2 slices (band-split); for nCoord>=3 it still returns 2 — callers must use the return value, not nCoord.
inline int partition(int nCoord, Slice* out) {
    if (nCoord <= 1) {
        out[0].nChan = 0;
        for (int i = 0; i < ui::CH24; i++)
            if (ui::CHAN24[i] != mesh::REPORT_CHAN) out[0].chans[out[0].nChan++] = ui::CHAN24[i];
        for (int i = 0; i < ui::CH5; i++) out[0].chans[out[0].nChan++] = ui::CHAN5[i];
        out[0].reportChan = SOCIAL[0];     // ch6; the CYD collects on REPORT_CHAN
        return 1;
    }
    out[0].nChan = 0;
    for (int i = 0; i < ui::CH24; i++)
        if (ui::CHAN24[i] != mesh::REPORT_CHAN) out[0].chans[out[0].nChan++] = ui::CHAN24[i];
    out[0].reportChan = SOCIAL[0];         // ch6
    out[1].nChan = 0;
    for (int i = 0; i < ui::CH5; i++) out[1].chans[out[1].nChan++] = ui::CHAN5[i];
    out[1].reportChan = ui::CHAN5[0];      // 36
    return 2;
}

} // namespace chanplan
