// Native DOMAIN range contract, shared by input and map projection.
#pragma once
namespace domr {
// --- range ladder: -/+ steps to an ADJACENT EXISTING range, clamped ----------
// The ranges are discrete (the device only keeps a projection cache per range),
// so -/+ select the neighbour in this fixed ladder rather than add/subtract an
// arbitrary metre count. Out-of-ladder stored values snap to the nearest rung.
static constexpr int kRanges[] = {10, 25, 50, 100, 250, 500, 1000, 2000};
static constexpr int kNRanges  = (int)(sizeof(kRanges) / sizeof(kRanges[0]));

inline int rangeIndex(int rangeM) {               // nearest existing rung
    int best = 0, bestd = kRanges[0] > rangeM ? kRanges[0] - rangeM : rangeM - kRanges[0];
    for (int i = 1; i < kNRanges; i++) {
        const int d = kRanges[i] > rangeM ? kRanges[i] - rangeM : rangeM - kRanges[i];
        if (d < bestd) { bestd = d; best = i; }
    }
    return best;
}
inline int rangeStep(int rangeM, int dir) {        // dir -1/+1; clamp at endpoints
    int i = rangeIndex(rangeM) + (dir < 0 ? -1 : 1);
    if (i < 0) i = 0; else if (i >= kNRanges) i = kNRanges - 1;
    return kRanges[i];
}

inline int rangeAt(int index) {
    return kRanges[index<0?0:index>=kNRanges?kNRanges-1:index];
}
inline int rangeIndexStep(int index,int dir) {
    const int next=index+(dir<0?-1:1);
    return next<0?0:next>=kNRanges?kNRanges-1:next;
}
} // namespace domr
