#include "../device/phase.h"
#include <stdio.h>
static int fails = 0;
static void check(const char* w, bool ok){ printf(ok?"  ok   %s\n":"  FAIL %s\n",w); if(!ok)fails++; }
int main() {
    using mesh_phase::offset;
    check("n=0 -> 0 (no divide by zero)", offset(0, 0, 2500) == 0);
    check("n=1 -> 0",                      offset(0, 1, 2500) == 0);
    check("4 nodes, idx0 -> 0",            offset(0, 4, 2500) == 0);
    check("4 nodes, idx1 -> 625",          offset(1, 4, 2500) == 625);
    check("4 nodes, idx3 -> 1875",         offset(3, 4, 2500) == 1875);
    check("idx>=n clamps into window",     offset(9, 4, 2500) < 2500);
    printf(fails?"\n%d FAILED\n":"\nall passed\n", fails);
    return fails ? 1 : 0;
}
