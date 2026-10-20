// 12 nodes across coordinators, end to end -- the fleet-scale behaviour we
// cannot put on a bench without a dozen radios, exercised through the SAME code
// the firmware runs: balance::pick decides which coordinator each node joins,
// chanplan::partition slices the spectrum, chanplan::planCluster hands each
// cluster's nodes their channels, and mesh_phase::offset staggers their reports.
//
// Printed as tables, like test_chanplan: the allocation being legible is itself
// the evidence that the policy is sane at scale.
#include "../device/balance.h"
#include "../device/chanplan.h"
#include "../device/phase.h"
#include "../device/mesh.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    if (!ok) { printf("  FAIL %s\n", what); fails++; }
}

// Replay the collector's new-node path: each node in turn joins the coordinator
// balance::pick names, which then carries one more node. assign[i] is the
// coordinator node i landed on (or -1 if it could not be placed).
static void balanceFleet(int nNodes, const uint8_t* wants5,
                         int nCoord, const chanplan::Slice* sl, int nSlice,
                         uint16_t* countOut, int* assign) {
    bool     live[2] = {false, false};
    uint16_t cnt[2]  = {0, 0};
    uint8_t  rc[2]   = {0, 0};
    for (int i = 0; i < nCoord && i < 2; i++) live[i] = true;
    for (int i = 0; i < nSlice && i < 2; i++) rc[i] = sl[i].reportChan;
    for (int n = 0; n < nNodes; n++) {
        int c = balance::pick(wants5[n], live, cnt, rc, nCoord, nSlice);
        assign[n] = c;
        if (c >= 0) cnt[c]++;
    }
    for (int i = 0; i < nCoord && i < 2; i++) countOut[i] = cnt[i];
}

int main() {
    const int pool = (ui::CH24 - 1) + ui::CH5;   // every channel the CYD is not camping
    printf("fleet of 12 nodes; %d channels to cover\n\n", pool);

    uint8_t all5[12]; for (int i = 0; i < 12; i++) all5[i] = 1;   // every node dual-band

    // --- 1 coordinator: all 12 land on it, and its single cluster covers the
    //     whole pool with no idle node and nobody over MAX_ASSIGN. ------------
    {
        chanplan::Slice sl[2];
        const int ns = chanplan::partition(1, sl);
        uint16_t cnt[2] = {0, 0}; int assign[12];
        balanceFleet(12, all5, 1, sl, ns, cnt, assign);
        printf("1 coordinator: %u nodes\n", cnt[0]);
        check("1 coord: all 12 nodes placed", cnt[0] == 12);

        chanplan::Plan plans[16];
        chanplan::planCluster(sl[0].chans, sl[0].nChan, 12, plans);
        int covered = 0, maxN = 0, idle = 0, seen[256] = {0}; bool dup = false;
        for (int i = 0; i < 12; i++) {
            if (plans[i].n == 0) idle++;
            if (plans[i].n > maxN) maxN = plans[i].n;
            covered += plans[i].n;
            for (int k = 0; k < plans[i].n; k++) if (seen[plans[i].ch[k]]++) dup = true;
        }
        check("1 coord: whole pool covered once", covered == pool && !dup);
        check("1 coord: no idle node",            idle == 0);
        check("1 coord: nobody exceeds MAX_ASSIGN", maxN <= mesh::MAX_ASSIGN);
    }

    // --- 2 coordinators: 12 dual-band nodes split 6/6, each cluster covers its
    //     own band slice, and the two clusters never touch the same channel. --
    {
        chanplan::Slice sl[2];
        const int ns = chanplan::partition(2, sl);
        check("2 coords: partition made 2 slices", ns == 2);
        uint16_t cnt[2] = {0, 0}; int assign[12];
        balanceFleet(12, all5, 2, sl, ns, cnt, assign);
        printf("2 coordinators: %u + %u nodes\n", cnt[0], cnt[1]);
        check("2 coords: all 12 placed", cnt[0] + cnt[1] == 12);
        check("2 coords: balanced within one",
              (cnt[0] > cnt[1] ? cnt[0] - cnt[1] : cnt[1] - cnt[0]) <= 1);
        check("2 coords: even 6/6 split", cnt[0] == 6 && cnt[1] == 6);

        int crossSeen[256] = {0}; bool crossDup = false;
        int totalUnique = 0;
        for (int c = 0; c < 2; c++) {
            chanplan::Plan plans[16];
            chanplan::planCluster(sl[c].chans, sl[c].nChan, cnt[c], plans);
            int covered = 0, idle = 0, seen[256] = {0}; bool dup = false, offBand = false;
            printf("  cluster %d (report ch%u, %u ch):\n", c, sl[c].reportChan, sl[c].nChan);
            for (int i = 0; i < (int)cnt[c]; i++) {
                printf("     node %2d  %-5s %2u :", i, plans[i].camp ? "CAMP" : "sweep", plans[i].n);
                for (int k = 0; k < plans[i].n; k++) printf(" %u", plans[i].ch[k]);
                printf("\n");
                if (plans[i].n == 0) idle++;
                covered += plans[i].n;
                for (int k = 0; k < plans[i].n; k++) {
                    const uint8_t ch = plans[i].ch[k];
                    if (seen[ch]++)      dup = true;
                    if (crossSeen[ch]++) crossDup = true;   // seen in the OTHER cluster too
                    totalUnique++;
                    const bool is24 = ch <= 14;
                    if ((sl[c].reportChan <= 14) != is24) offBand = true;
                }
            }
            char lbl[64];
            snprintf(lbl, sizeof lbl, "cluster %d: covers its slice once", c);
            check(lbl, covered == sl[c].nChan && !dup);
            snprintf(lbl, sizeof lbl, "cluster %d: no idle node", c);
            check(lbl, idle == 0);
            snprintf(lbl, sizeof lbl, "cluster %d: stays in its band", c);
            check(lbl, !offBand);
        }
        check("2 coords: clusters never share a channel", !crossDup);
        check("2 coords: together they cover the whole pool", totalUnique == pool);
    }

    // --- mixed radios: 4 of the 12 are 2.4GHz-only. They must never be sent to
    //     the 5GHz cluster; the 5GHz coordinator takes only dual-band nodes. ---
    {
        chanplan::Slice sl[2];
        const int ns = chanplan::partition(2, sl);
        // coordinator 0 is the 2.4GHz slice, coordinator 1 the 5GHz slice.
        const int band5 = sl[0].reportChan > 14 ? 0 : 1;
        uint8_t w5[12]; for (int i = 0; i < 12; i++) w5[i] = (i < 4) ? 0 : 1;  // first 4 are 2.4-only
        uint16_t cnt[2] = {0, 0}; int assign[12];
        balanceFleet(12, w5, 2, sl, ns, cnt, assign);
        printf("mixed: 4 2.4-only + 8 dual-band -> %u + %u\n", cnt[0], cnt[1]);
        bool twoFourOn5 = false;
        for (int i = 0; i < 12; i++) if (!w5[i] && assign[i] == band5) twoFourOn5 = true;
        check("mixed: no 2.4-only node on the 5GHz cluster", !twoFourOn5);
        check("mixed: every node still placed", cnt[0] + cnt[1] == 12);
    }

    // --- report phase staggering: a cluster's nodes spread across the window,
    //     distinct and ordered, so their reports do not all land together. ----
    {
        for (int n = 2; n <= 12; n++) {
            uint32_t prev = 0; bool ordered = true, inWindow = true;
            uint32_t minGap = mesh::REPORT_MS, maxOff = 0;
            for (int i = 0; i < n; i++) {
                const uint32_t o = mesh_phase::offset(i, n, mesh::REPORT_MS);
                if (o >= mesh::REPORT_MS) inWindow = false;
                if (i > 0) { if (o <= prev) ordered = false; if (o - prev < minGap) minGap = o - prev; }
                if (o > maxOff) maxOff = o;
                prev = o;
            }
            char lbl[64];
            snprintf(lbl, sizeof lbl, "%2d nodes: offsets ordered and within the window", n);
            check(lbl, ordered && inWindow);
            // Evenly spread: the smallest gap is close to window/n, not bunched.
            snprintf(lbl, sizeof lbl, "%2d nodes: reports spread, not bunched", n);
            check(lbl, minGap >= (mesh::REPORT_MS / n) - 1);
        }
        printf("phase: 12-node window spread min gap %u ms over %u ms\n",
               mesh::REPORT_MS / 12, mesh::REPORT_MS);
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
