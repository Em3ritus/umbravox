// Host test: assert layout_engine.h reproduces Codex's geometry.json EXACTLY
// for the two approved profiles (480x320, 320x240). Build + run:
//   g++ -std=c++17 -I.. sim/test_layout_engine.cpp -o /tmp/tle && /tmp/tle
#include "../ui/layout_engine.h"
#include <cstdio>
using namespace uieng;

static int fails = 0, checks = 0;

static void ck(const char* tag, Rect got, int x, int y, int w, int h) {
    const Rect exp{x, y, w, h};
    ++checks;
    if (got == exp) { printf("  ok   %-14s (%d,%d,%d,%d)\n", tag, got.x, got.y, got.w, got.h); }
    else { ++fails; printf("  FAIL %-14s got (%d,%d,%d,%d) exp (%d,%d,%d,%d)\n",
                           tag, got.x, got.y, got.w, got.h, x, y, w, h); }
}
static void ckb(const char* tag, bool got, bool exp) {
    ++checks;
    if (got == exp) printf("  ok   %-14s %s\n", tag, got ? "true" : "false");
    else { ++fails; printf("  FAIL %-14s got %s exp %s\n", tag, got?"true":"false", exp?"true":"false"); }
}
static void chromeCk(const ScanLayout& s, int W, int H, int cw) {
    ck("status", s.status, 0,0,W,44);
    ck("nav", s.nav, 0,H-44,W,44);
    ck("body", s.body, 8,52,W-16,H-104);
    for (int i=0;i<4;i++){ ck("statusCell", s.statusCells[i], i*cw,0,cw,44);
                           ck("navCell", s.navCells[i], i*cw,H-44,cw,44); }
}

int main() {
    // ---- SCAN 480x320 ----
    printf("SCAN 480x320\n");
    { ScanLayout s = scan(480,320);
      chromeCk(s,480,320,120);
      ck("counts", s.counts, 8,52,111,216);
      ck("hero",   s.hero,   127,52,345,216);
      ck("tile0",  s.tiles[0], 8,52,111,72);
      ck("tile1",  s.tiles[1], 8,124,111,72);
      ck("tile2",  s.tiles[2], 8,196,111,72); }

    // ---- SCAN 320x240 ----
    printf("SCAN 320x240\n");
    { ScanLayout s = scan(320,240);
      chromeCk(s,320,240,80);
      ck("counts", s.counts, 8,52,88,136);
      ck("hero",   s.hero,   104,52,208,136);
      ck("tile0",  s.tiles[0], 8,52,88,45);
      ck("tile1",  s.tiles[1], 8,97,88,45);
      ck("tile2",  s.tiles[2], 8,142,88,46); }

    // ---- DOMAIN 480x320 ----
    printf("DOMAIN 480x320\n");
    { DomainLayout d = domain(480,320);
      ck("map",      d.map,      8,52,312,216);
      ck("contacts", d.contacts, 328,52,144,168);
      ck("range",    d.range,    328,224,144,44);
      ck("minus",    d.minus,    328,224,44,44);
      ck("plus",     d.plus,     428,224,44,44);
      ckb("wide",    d.wide,     true); }

    // ---- DOMAIN 320x240 ----
    printf("DOMAIN 320x240\n");
    { DomainLayout d = domain(320,240);
      ck("map",      d.map,      8,52,184,136);
      ck("contacts", d.contacts, 200,52,112,88);
      ck("range",    d.range,    200,144,112,44);
      ck("minus",    d.minus,    200,144,44,44);
      ck("plus",     d.plus,     268,144,44,44);
      ckb("wide",    d.wide,     false); }

    printf("\n%s  %d/%d checks passed\n", fails ? "FAILED" : "PASSED", checks - fails, checks);
    return fails ? 1 : 0;
}
