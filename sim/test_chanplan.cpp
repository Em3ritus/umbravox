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

    for (int live = 1; live <= 6; live++) {
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

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
