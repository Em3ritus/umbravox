// Host tests for the pure parts of the perf log: the interval accumulator and
// the CSV row. The SD batching and the metric sources live in perflog.cpp and
// are device-only.
#include "../device/perflog.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static void check(const char* w, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", w);
    if (!ok) fails++;
}
static int fields(const char* s) {            // comma-separated field count
    int n = 1;
    for (; *s && *s != '\n'; s++) if (*s == ',') n++;
    return n;
}
// The Nth (0-based) field of a row, copied out.
static void field(const char* s, int n, char* out, size_t cap) {
    for (; n > 0 && *s; s++) if (*s == ',') n--;
    size_t i = 0;
    while (*s && *s != ',' && *s != '\n' && i + 1 < cap) out[i++] = *s++;
    out[i] = 0;
}
static int col(const char* name) {            // column index by header name
    const char* h = perflog::header();
    char tok[24]; int idx = 0;
    for (;;) {
        field(h, idx, tok, sizeof tok);
        if (!tok[0]) return -1;
        if (!strcmp(tok, name)) return idx;
        idx++;
    }
}

int main() {
    using namespace perflog;
    char b[300], f[32];

    printf("accumulator:\n");
    Acc a;
    a.frame(10, 2); a.frame(14, 6); a.frame(6, 1);
    check("frame count",        a.frames == 3);
    check("draw max",           a.drawMax == 14);
    check("blit max",           a.blitMax == 6);
    a.gap(12); a.gap(240); a.gap(30);
    check("worst gap is kept",  a.gapMax == 240);
    a.sd(5); a.sd(21); a.sd(3);
    check("worst SD stall kept", a.sdMax == 21);
    a.clear();
    check("clear resets it",    a.frames == 0 && a.gapMax == 0 && a.sdMax == 0 && a.drawMax == 0);

    printf("row:\n");
    Acc r;
    for (int i = 0; i < 300; i++) r.frame(i % 2 ? 12 : 8, i % 3);   // 300 frames, avg draw 10
    r.gap(75);
    Snap s;
    s.upS = 120; s.heapFree = 65000; s.heapMin = 60000; s.heapBlock = 40000;
    s.psramFree = 7000000; s.stackFree = 2100;
    s.wifiFrames = 500; s.bleAdverts = 90; s.reports = 4;
    s.wifiUniq = 4096; s.tableCap = 8192; s.nodes = 2; s.lossPct = 7;
    s.camped = true; s.fix = true; s.theme = 2; s.sweep = 5;
    const int n = row(b, sizeof b, r, s, 10000);
    check("row ends with a newline",       n > 0 && b[n - 1] == '\n');
    check("row has one field per header column", fields(b) == fields(header()));
    field(b, col("fps"), f, sizeof f);
    check("30 fps from 300 frames in 10 s", !strcmp(f, "30.0"));
    field(b, col("draw_avg"), f, sizeof f);
    check("average draw is 10 ms",          !strcmp(f, "10"));
    field(b, col("gap_max"), f, sizeof f);
    check("worst gap lands in its column",  !strcmp(f, "75"));
    field(b, col("wifi_fps"), f, sizeof f);
    check("500 frames / 10 s = 50 per s",   !strcmp(f, "50"));
    field(b, col("tbl_pct"), f, sizeof f);
    check("dedup table 50% full",           !strcmp(f, "50"));
    field(b, col("heap_min"), f, sizeof f);
    check("heap minimum recorded",          !strcmp(f, "60000"));

    printf("defensive:\n");
    Acc z; Snap zs; zs.dark = true;                       // a dark interval: no frames at all
    const int nz = row(b, sizeof b, z, zs, 10000);
    check("no frames -> still a full row, no divide by zero", nz > 0 && fields(b) == fields(header()));
    field(b, col("fps"), f, sizeof f);
    check("dark interval reports 0.0 fps",  !strcmp(f, "0.0"));
    field(b, col("dark"), f, sizeof f);
    check("dark flag set",                  !strcmp(f, "1"));
    check("zero-length interval is safe",   row(b, sizeof b, r, s, 0) > 0);
    s.tableCap = 0;
    check("empty table capacity is safe",   row(b, sizeof b, r, s, 10000) > 0);
    const int nt = row(b, 12, r, s, 10000);
    check("tiny buffer stays in bounds",    nt >= 0 && nt < 12 && strlen(b) < 12);
    check("zero-size buffer -> 0",          row(b, 0, r, s, 10000) == 0);

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
