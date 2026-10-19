// What the mesh does with N radios.
//
// Printed rather than only asserted: the point of this test is that the
// allocation is legible, and a table you can read is better evidence that
// the policy is sane than an equality check that passes.
#include "../device/chanplan.h"
#include <stdio.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    if (!ok) { printf("  FAIL %s\n", what); fails++; }
}

int main() {
    // Everything the Core is not holding: 2.4GHz minus channel 1, plus 5GHz.
    const int total = (ui::CH24 - 1) + ui::CH5;
    printf("core camps ch%d; %d channels to share\n\n", mesh::REPORT_CHAN, total);

    for (int live = 1; live <= 8; live++) {
        chanplan::Plan p[8];
        chanplan::planChannels(live, p);
        printf("%d node%s:\n", live, live == 1 ? " " : "s");

        int covered = 0, campers = 0;
        for (int i = 0; i < live; i++) {
            printf("   node %d  %-5s %2u ch :", i, p[i].camp ? "CAMP" : "sweep", p[i].n);
            for (int k = 0; k < p[i].n; k++) printf(" %u", p[i].ch[k]);
            printf("\n");
            covered += p[i].n;
            if (p[i].camp) campers++;
        }

        // Every channel assigned exactly once, and none of them the Core's.
        bool dupe = false, stoleReport = false;
        int seen[200] = {0};
        for (int i = 0; i < live; i++)
            for (int k = 0; k < p[i].n; k++) {
                if (p[i].ch[k] == mesh::REPORT_CHAN) stoleReport = true;
                if (seen[p[i].ch[k]]++) dupe = true;
            }
        check("no channel assigned twice", !dupe);
        check("report channel left to the core", !stoleReport);
        check("every channel covered", covered == total);
        // A node with one channel must be told to camp, not to "sweep" a
        // list of one.
        for (int i = 0; i < live; i++)
            check("single-channel node camps", !(p[i].n == 1 && !p[i].camp));
        // Breadth first: no radio is spent camping until there are three.
        if (live < 3) check("no campers below three radios", campers == 0);
        if (live >= 4) check("two campers at four or more", campers == 2);
        printf("\n");
    }

    // 9..12 nodes: every assigned channel array stays within MAX_ASSIGN, and every
    // channel in the pool is covered by at least one node.
    {
        for (int live = 9; live <= 12; live++) {
            chanplan::Plan plans[16];
            chanplan::planChannels(live, plans);
            int maxN = 0, totalCh = 0;
            for (int i = 0; i < live; i++) { if (plans[i].n > maxN) maxN = plans[i].n; totalCh += plans[i].n; }
            char label[64]; snprintf(label, sizeof label, "%d nodes: no node exceeds MAX_ASSIGN", live);
            check(label, maxN <= mesh::MAX_ASSIGN);
            snprintf(label, sizeof label, "%d nodes: something is assigned", live);
            check(label, totalCh > 0);
            snprintf(label, sizeof label, "%d nodes: every channel covered exactly once", live);
            check(label, totalCh == total);
            bool dupe = false, stoleReport = false, empty = false;
            int seen[200] = {0};
            for (int i = 0; i < live; i++) {
                if (plans[i].n == 0) empty = true;
                for (int k = 0; k < plans[i].n; k++) {
                    if (plans[i].ch[k] == mesh::REPORT_CHAN) stoleReport = true;
                    if (seen[plans[i].ch[k]]++) dupe = true;
                }
            }
            snprintf(label, sizeof label, "%d nodes: no duplicates, report chan left alone", live);
            check(label, !dupe && !stoleReport);
            snprintf(label, sizeof label, "%d nodes: no idle node", live);
            check(label, !empty);
        }
    }
    // planCluster over an explicit 2.4GHz-only slice splits it across the nodes.
    {
        const uint8_t slice[] = {2,3,4,5,7,8,9,10,12,13};
        chanplan::Plan plans[6];
        chanplan::planCluster(slice, 10, 6, plans);
        int total24 = 0; for (int i = 0; i < 6; i++) total24 += plans[i].n;
        check("planCluster covers the whole slice at least once", total24 >= 10);
        bool only24 = true;
        for (int i = 0; i < 6; i++) for (int k = 0; k < plans[i].n; k++) if (plans[i].ch[k] > 14) only24 = false;
        check("planCluster stays within its band slice", only24);
    }

    // Partitioning: 1 coordinator gets everything; 2 get disjoint slices with
    // distinct report channels (Review Focus 1).
    {
        chanplan::Slice s1[2];
        check("1 coordinator -> 1 slice", chanplan::partition(1, s1) == 1);
        // 12 non-collection 2.4GHz channels plus 8 5GHz channels.
        check("1 coordinator covers both bands",
              s1[0].nChan == (ui::CH24 - 1) + ui::CH5);
        bool s1HasReport = false;
        for (int i = 0; i < s1[0].nChan; i++) if (s1[0].chans[i] == mesh::REPORT_CHAN) s1HasReport = true;
        check("1 coordinator leaves the collection channel out", !s1HasReport);

        chanplan::Slice s2[2];
        check("2 coordinators -> 2 slices", chanplan::partition(2, s2) == 2);
        check("distinct report channels", s2[0].reportChan != s2[1].reportChan);
        check("report channels avoid the collection channel",
              s2[0].reportChan != mesh::REPORT_CHAN && s2[1].reportChan != mesh::REPORT_CHAN);
        check("slice 0 fits MAX_SLICE", s2[0].nChan <= mesh::MAX_SLICE);
        check("slice 1 fits MAX_SLICE", s2[1].nChan <= mesh::MAX_SLICE);
        // disjoint slices
        bool overlap = false;
        for (int i = 0; i < s2[0].nChan; i++)
            for (int j = 0; j < s2[1].nChan; j++)
                if (s2[0].chans[i] == s2[1].chans[j]) overlap = true;
        check("2 coordinators get disjoint channel slices", !overlap);
        // one is 2.4-only, the other 5GHz-only (band split)
        bool a24 = true, b5 = true;
        for (int i = 0; i < s2[0].nChan; i++) if (s2[0].chans[i] > 14) a24 = false;
        for (int j = 0; j < s2[1].nChan; j++) if (s2[1].chans[j] <= 14) b5 = false;
        check("2 coordinators split by band", a24 && b5);
    }

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
