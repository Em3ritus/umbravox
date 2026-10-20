#pragma once
// Pure node-to-coordinator balancer, lifted out of the collector so it can be
// unit-tested at scale without a radio (same move as chanplan.h and phase.h).
// The collector's pickCoord is now a thin wrapper that fills these arrays from
// its live coordinator table and calls pick(); the policy lives here.
#include <stdint.h>

namespace balance {

// The live coordinator carrying the fewest nodes, among those whose slice a
// node with this radio can actually cover: a 2.4-only node (wants5 == 0) never
// lands on a 5GHz slice (reportChan > 14). Ties go to the lower index, so a
// run of identical nodes fills coordinators round-robin and evenly. Returns -1
// when no coordinator can take the node.
//
//   wants5      the joining node can do 5GHz
//   live[i]     coordinator i is registered and holding a slice
//   nodes[i]    how many nodes are already pinned to coordinator i
//   reportChan[i]  coordinator i's report channel (>14 means its slice is 5GHz)
//   nCoord      coordinators in the table
//   nSlice      slices the partition produced (a coordinator past this has none)
inline int pick(uint8_t wants5, const bool* live, const uint16_t* nodes,
                const uint8_t* reportChan, int nCoord, int nSlice) {
    int best = -1;
    for (int i = 0; i < nCoord && i < nSlice; i++) {
        if (!live[i]) continue;
        if (!wants5 && reportChan[i] > 14) continue;  // 5GHz slice, 2.4-only node
        if (best < 0 || nodes[i] < nodes[best]) best = i;
    }
    return best;
}

} // namespace balance
