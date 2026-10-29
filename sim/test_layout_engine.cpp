// Host test: assert layout_engine.h reproduces Codex's geometry.json EXACTLY
// for the two approved profiles (480x320, 320x240). Build + run:
//   g++ -std=c++17 -I.. sim/test_layout_engine.cpp -o /tmp/tle && /tmp/tle
#include "../ui/layout_engine.h"
#include <cstdio>
#include <cstring>
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
static void ckHit(const HitList& hl, int i, const char* id, int x, int y, int w, int h, const char* act) {
    ++checks;
    if (i >= hl.count) { ++fails; printf("  FAIL hit[%d]       missing (count=%d)\n", i, hl.count); return; }
    const Hit& g = hl.items[i];
    const Rect e{x, y, w, h};
    if (std::strcmp(g.id,id)==0 && g.rect==e && std::strcmp(g.action,act)==0)
        printf("  ok   hit %-12s (%d,%d,%d,%d)\n", g.id, g.rect.x, g.rect.y, g.rect.w, g.rect.h);
    else { ++fails; printf("  FAIL hit[%d] %-10s got {%s,(%d,%d,%d,%d)}\n", i, id,
                           g.id, g.rect.x, g.rect.y, g.rect.w, g.rect.h); }
}
static void ckCount(const char* tag, int got, int exp) {
    ++checks;
    if (got == exp) printf("  ok   %-14s count=%d\n", tag, got);
    else { ++fails; printf("  FAIL %-14s got %d exp %d\n", tag, got, exp); }
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

    // ---- SCAN hits (id + rect + action) ----
    printf("SCAN hits 480x320\n");
    { HitList h = scanHits(480,320);
      ckCount("count", h.count, 11);
      ckHit(h,0,"gps",0,0,120,44,"Open GPS diagnostics");
      ckHit(h,3,"logging",360,0,120,44,"Toggle logging; apply existing logger behavior");
      ckHit(h,4,"nav-scan",0,276,120,44,"Open SCAN");
      ckHit(h,8,"WIFI",8,52,111,72,"Open Codex filtered to WIFI");
      ckHit(h,10,"BLE",8,196,111,72,"Open Codex filtered to BLE"); }
    printf("SCAN hits 320x240\n");
    { HitList h = scanHits(320,240);
      ckCount("count", h.count, 11);
      ckHit(h,7,"nav-menu",240,196,80,44,"Open MENU");
      ckHit(h,8,"WIFI",8,52,88,45,"Open Codex filtered to WIFI"); }

    // ---- DOMAIN hits (wide: header + contact rows) ----
    printf("DOMAIN hits 480x320\n");
    { HitList h = domainHits(480,320);
      ckCount("count", h.count, 13);
      ckHit(h,8,"range-minus",328,224,44,44,"Select adjacent existing range; clamp at endpoint");
      ckHit(h,9,"range-plus",428,224,44,44,"Select adjacent existing range; clamp at endpoint");
      ckHit(h,10,"all-contacts",328,52,144,44,"Open Codex with located scope");
      ckHit(h,11,"row",328,96,144,44,"Open mapped device detail by stable identity");
      ckHit(h,12,"row",328,140,144,44,"Open mapped device detail by stable identity"); }

    printf("\n%s  %d/%d checks passed\n", fails ? "FAILED" : "PASSED", checks - fails, checks);
    return fails ? 1 : 0;
}
