// Host test for the WiGLE CSV formatters. These are pure and format-critical:
// a silent error here is a drive that will not upload, so they are pinned even
// though the code is already written.
#include "../device/csv.h"
#include "../ui/gnss.h"     // toEpoch, so the timestamp test is a round-trip
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}
static bool eq(const char* a, const char* b) { return strcmp(a, b) == 0; }

int main() {
    char out[64];

    printf("deg7: fixed-point degrees, 7 places:\n");
    csv::deg7(out, 377749000);   check("37.7749 positive", eq(out, "37.7749000"));
    csv::deg7(out, -1224194000); check("-122.4194 negative", eq(out, "-122.4194000"));
    csv::deg7(out, 0);           check("zero", eq(out, "0.0000000"));
    csv::deg7(out, 5);           check("tiny fraction keeps leading zeros", eq(out, "0.0000005"));

    printf("quote: RFC 4180:\n");
    csv::quote("plain", out, sizeof out);        check("plain still quoted", eq(out, "\"plain\""));
    csv::quote("a,b", out, sizeof out);          check("comma", eq(out, "\"a,b\""));
    csv::quote("say \"hi\"", out, sizeof out);   check("inner quotes doubled", eq(out, "\"say \"\"hi\"\"\""));
    csv::quote("", out, sizeof out);             check("empty", eq(out, "\"\""));

    printf("stampUtc: round-trips with the project's own toEpoch:\n");
    // No magic epoch literal: toEpoch builds it, stampUtc must read it back.
    // This also pins that the two converters agree, which the filename (Task 4)
    // and firstEpoch both rely on.
    csv::stampUtc(out, ui::Gnss::toEpoch(2026, 10, 18, 14, 23, 45));
    check("2026-10-18 14:23:45 round-trips", eq(out, "2026-10-18 14:23:45"));
    csv::stampUtc(out, ui::Gnss::toEpoch(2000, 1, 1, 0, 0, 0));
    check("millennium midnight", eq(out, "2000-01-01 00:00:00"));
    csv::stampUtc(out, 0u);
    check("epoch 0 is 1970", eq(out, "1970-01-01 00:00:00"));

    printf("mac:\n");
    const uint8_t m[6] = {0x00,0x11,0x22,0xAB,0xCD,0xEF};
    csv::mac(out, m);  check("uppercase colon-separated", eq(out, "00:11:22:AB:CD:EF"));

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
