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

// `campers` is how many of SOCIAL are getting a dedicated radio. Only those
// come out of the sweep pool -- taking out both when only one is camped
// leaves the other covered by nobody, which is exactly what it did.
inline void bandLists(uint8_t* l24, int& n24, uint8_t* l5, int& n5, int campers) {
    n24 = n5 = 0;
    for (int i = 0; i < ui::CH24; i++) {
        const uint8_t ch = ui::CHAN24[i];
        if (ch == mesh::REPORT_CHAN) continue;          // the Core holds this
        bool camped = false;
        for (int c = 0; c < campers; c++) if (ch == SOCIAL[c]) camped = true;
        if (camped) continue;
        l24[n24++] = ch;
    }
    for (int i = 0; i < ui::CH5; i++) l5[n5++] = ui::CHAN5[i];
}

// Fill `plans` for `live` nodes.
inline void planChannels(int live, Plan* plans) {
    for (int i = 0; i < live; i++) { plans[i].n = 0; plans[i].camp = false; }
    if (live <= 0) return;

    // Dedicated campers only once there are three or more radios: with one
    // or two, every channel spent camping is breadth we cannot spare.
    const int campers = live >= 4 ? 2 : live == 3 ? 1 : 0;
    for (int i = 0; i < campers; i++) {
        plans[i].n = 1;
        plans[i].ch[0] = SOCIAL[i];
        plans[i].camp = true;
    }

    uint8_t l24[16], l5[16];
    int n24 = 0, n5 = 0;
    bandLists(l24, n24, l5, n5, campers);

    const int sweepers = live - campers;
    if (sweepers <= 0) return;

    if (sweepers == 1) {
        // One sweeper takes everything, band change per wrap and all --
        // there is no second radio to split with.
        Plan& p = plans[campers];
        for (int i = 0; i < n24 && p.n < mesh::MAX_ASSIGN; i++) p.ch[p.n++] = l24[i];
        for (int i = 0; i < n5  && p.n < mesh::MAX_ASSIGN; i++) p.ch[p.n++] = l5[i];
        return;
    }

    // Two or more: split by band, in proportion to how many channels each
    // band has, with at least one radio on each so neither goes unheard.
    int s24 = (sweepers * n24 + (n24 + n5) / 2) / (n24 + n5);
    if (s24 < 1) s24 = 1;
    if (s24 > sweepers - 1) s24 = sweepers - 1;
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


} // namespace chanplan
