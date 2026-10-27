// Host test: a theme pack that restates a built-in theme's settings renders
// PIXEL FOR PIXEL like the built-in. The strongest integration check of the
// pack path: parser -> admission -> pal::addPack -> geometry, colours,
// centrepiece, motif and alert dispatch, all against the real drawing code.
//
// Fixtures are sim/packs/*; PACKS_DIR (default "packs") is relative to the
// working directory, which is sim/ under `make -C sim test`.
//
// Not compared: the ASPECT screen, whose THEME row prints the theme's NAME
// ("UMBRAVOX CLONE" vs "UMBRAVOX"), and so legitimately differs. MESH is
// compared (it has no name text).
#include "packload.h"
#include "../ui/screens.h"
#include "../ui/demo.h"
#include "../ui/nav.h"
#include "../ui/palette.h"
#include "../ui/signatures.h"
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#ifndef PACKS_DIR
#define PACKS_DIR "packs"
#endif

static int fails = 0;
static void check(const char* w, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", w);
    if (!ok) fails++;
}

using namespace ui;

static Canvas A, B;

static void scan(Canvas& c, uint8_t theme, uint32_t frame) {
    pal::setTheme(theme);
    State s = demoState(frame);
    s.set.theme = theme; s.set.sweep = SW_RADAR; s.frame = frame;
    s.screen = SCR_SCAN;
    c.clear(0);
    drawScan(c, s, nullptr);
}
static void alert(Canvas& c, uint8_t theme, int age) {
    pal::setTheme(theme);
    State s = demoState(1000);
    s.set.theme = theme; s.set.sweep = SW_RADAR;
    s.set.alertStyle = 0;                      // COSMIC MUFFIN: 0 = supernova
    s.alertActive = true; s.alertThreat = TH_DRONE; s.alertConf = CONF_HIGH;
    s.alertAge = (uint16_t)age;
    strcpy(s.alertWhy, "DRONEID"); strcpy(s.alertName, "60:60:1F:A2:4C:19");
    c.clear(0);
    drawScan(c, s, nullptr);
}
static void mesh(Canvas& c, uint8_t theme) {
    pal::setTheme(theme);
    State s = demoState(0);
    s.set.theme = theme; s.screen = SCR_MESH;
    c.clear(0);
    drawMesh(c, s);
}

static int diff(const Canvas& a, const Canvas& b) {
    int n = 0;
    for (int i = 0; i < W * H; i++) if (a.px[i] != b.px[i]) n++;
    return n;
}

static void parity(const char* fixture, uint8_t builtin) {
    char path[256], why[96] = "";
    snprintf(path, sizeof path, "%s/%s", PACKS_DIR, fixture);
    const int id = packload::load(path, why, sizeof why);
    printf("%s -> theme %d %s\n", fixture, id, id < 0 ? why : "");
    check("fixture loads", id >= pal::TH_COUNT);
    if (id < 0) return;
    char w[96];
    for (uint32_t f : {0u, 24u, 600u}) {
        scan(A, (uint8_t)id, f); scan(B, builtin, f);
        snprintf(w, sizeof w, "%s scan frame %u identical", fixture, f);
        const int d = diff(A, B);
        if (d) printf("       %d pixels differ\n", d);
        check(w, d == 0);
    }
    for (int age : {100, 400, 900}) {
        alert(A, (uint8_t)id, age); alert(B, builtin, age);
        snprintf(w, sizeof w, "%s alert age %d identical", fixture, age);
        const int d = diff(A, B);
        if (d) printf("       %d pixels differ\n", d);
        check(w, d == 0);
    }
    mesh(A, (uint8_t)id); mesh(B, builtin);
    snprintf(w, sizeof w, "%s mesh frame 0 identical", fixture);
    check(w, diff(A, B) == 0);
}

// --- the loader against hostile folders: each must fail cleanly, never crash.
static void put(const char* dir, const char* name, const char* data, size_t n) {
    char p[300]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE* f = fopen(p, "wb"); if (f) { fwrite(data, 1, n, f); fclose(f); }
}
static void hostile() {
    char root[] = "/tmp/uvpackXXXXXX";
    if (!mkdtemp(root)) { check("mkdtemp", false); return; }
    char d[300], why[96];
    auto fresh = [&](const char* name) { snprintf(d, sizeof d, "%s/%s", root, name); mkdir(d, 0755); };
    auto bad = [&](const char* label) {
        why[0] = 0;
        const int id = packload::load(d, why, sizeof why);
        printf("       %s: \"%s\"\n", label, why);
        check(label, id < 0 && why[0]);
    };
    const int before = pal::themeCount();

    snprintf(d, sizeof d, "%s/absent", root);          bad("missing folder");
    fresh("empty");                                    bad("empty folder");
    fresh("dirjson"); { char p[400]; snprintf(p, sizeof p, "%s/theme.json", d); mkdir(p, 0755); }
    bad("theme.json is a directory");
    fresh("huge");
    { static char big[5u << 20]; memset(big, ' ', sizeof big); put(d, "theme.json", big, sizeof big); }
    bad("5 MB theme.json (size checked before reading)");
    fresh("noread"); put(d, "theme.json", "{}", 2);
    { char p[400]; snprintf(p, sizeof p, "%s/theme.json", d); chmod(p, 0); }
    if (geteuid() == 0) printf("       (root: unreadable-file case skipped)\n"); else bad("unreadable theme.json");
    fresh("garbage"); put(d, "theme.json", "not json", 8);
    bad("garbage theme.json");
    snprintf(d, sizeof d, "%s/bad name", root);        mkdir(d, 0755); bad("folder name with a space");
    snprintf(d, sizeof d, "%s/..", root);              bad("folder named ..");
    // emblem centre without emblem.bin, and with a corrupt one
    { FILE* f = fopen("packs/cosmic-clone/theme.json", "rb"); static char j[4096]; size_t n = f ? fread(j, 1, sizeof j, f) : 0; if (f) fclose(f);
      fresh("noemblem"); put(d, "theme.json", j, n); bad("emblem centre without emblem.bin");
      fresh("badembl");  put(d, "theme.json", j, n); put(d, "emblem.bin", "UVE", 3); bad("corrupt emblem.bin"); }
    check("no failed load registered a theme", pal::themeCount() == before);
    char cmd[400]; snprintf(cmd, sizeof cmd, "chmod -R u+rwx %s; rm -rf %s", root, root);
    if (system(cmd)) printf("       (cleanup of %s failed)\n", root);
}

int main() {
    hostile();
    parity("umbravox-clone", pal::TH_UMBRAVOX);
    parity("cosmic-clone", pal::TH_COSMICMUFFIN);
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
