// Host test: tapping an alert dismisses it, and it stays dismissed while the SAME threat keeps being re-detected;
// a different threat or name, or the alert ending, brings alerts back.
// Build: make -C sim test_alert_gate
#include "../ui/alert_gate.h"
#include <cstdio>
static int fails = 0;
static void expect(bool c, const char* m) { if (!c) { ++fails; printf("  FAIL %s\n", m); } else printf("  ok   %s\n", m); }
int main() {
    alertgate::Gate g;
    expect(g.shows(true, 3, "CAM-01"), "a fresh alert shows");
    expect(!g.shows(false, 0, ""), "no alert, nothing to show");
    g.dismiss(3, "CAM-01");
    expect(!g.shows(true, 3, "CAM-01"), "dismissed: the same alert re-detected stays hidden");
    expect(!g.shows(true, 3, "CAM-01"), "...and keeps staying hidden");
    expect(g.shows(true, 4, "CAM-01"), "a different threat shows again");
    g.dismiss(4, "CAM-01");
    expect(g.shows(true, 4, "CAM-02"), "a different name shows again");
    g.dismiss(4, "CAM-02"); (void)g.shows(false, 0, "");
    expect(g.shows(true, 4, "CAM-02"), "once the alert has ended, the same alert later shows again");
    printf(fails ? "FAILED\n" : "PASSED\n"); return fails ? 1 : 0;
}
