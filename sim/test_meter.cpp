// Plan 5c: meter::run replays a layout's scan or alert frames through the board's own slice loop and counts the
// pixels drawn and pushed per frame (the designer's speed meter, via the uv_meter wasm export).
#include "../ui/meter.h"
#include "../ui/preview_ws.h"
#include "../ui/themelayout.h"
#include "packload.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/stat.h>

static int fails = 0;
static void expect(bool c, const char* m) { if (!c) { ++fails; printf("  FAIL %s\n", m); } else printf("  ok   %s\n", m); }

static std::vector<uimeter::Count> run(int theme, int w, int h, int mode, int frames = 30, int frameMs = 40) {
    std::vector<uimeter::Count> v(frames);
    const int n = meter::run(theme, w, h, mode, frames, frameMs, v.data());
    v.resize(n < 0 ? 0 : n);
    return v;
}
static uint32_t worstPushed(const std::vector<uimeter::Count>& v, int from = 0) {
    uint32_t m = 0; for (size_t i = from; i < v.size(); i++) if (v[i].pushed > m) m = v[i].pushed; return m;
}
static uint32_t worstDrawn(const std::vector<uimeter::Count>& v, int from = 0) {
    uint32_t m = 0; for (size_t i = from; i < v.size(); i++) if (v[i].drawn > m) m = v[i].drawn; return m;
}
static bool same(const std::vector<uimeter::Count>& a, const std::vector<uimeter::Count>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) if (a[i].drawn != b[i].drawn || a[i].pushed != b[i].pushed) return false;
    return true;
}

static std::string root;
static void writeFile(const std::string& path, const std::string& body) {
    FILE* f = fopen(path.c_str(), "wb"); fwrite(body.data(), 1, body.size(), f); fclose(f);
}
static std::string themeJson(const char* name) {
    std::string s = std::string("{\"format\":1,\"name\":\"") + name + "\",\"dial\":\"compact\",\"centrepiece\":\"heptagram\","
                    "\"sweeps\":[\"radar\"],\"alert\":\"ward\",\"colors\":{";
    static const char* roles[] = {"void", "panel", "grid", "edge", "edgeLit", "dim", "text", "bright", "witch", "witchHi",
                                  "bone", "sweep", "sweepHi", "live", "liveHi", "magenta", "ice", "green", "amber", "red"};
    for (int i = 0; i < 20; i++) s += std::string(i ? "," : "") + "\"" + roles[i] + "\":\"" + (i == 0 ? "#000000" : "#C0C0C0") + "\"";
    return s + "}}";
}
static int pack(const char* folder, const char* name, const char* layout) {
    const std::string d = root + "/" + folder;
    mkdir(d.c_str(), 0755);
    writeFile(d + "/theme.json", themeJson(name));
    writeFile(d + "/layout.json", layout);
    char why[128];
    const int id = packload::load(d.c_str(), why, sizeof why);
    if (id < 0) printf("    pack %s refused: %s\n", folder, why);
    return id;
}

static void board(int w, int h, const char* name) {
    printf("%s %dx%d\n", name, w, h);
    const int t = pal::TH_UMBRAVOX;
    const auto scan = run(t, w, h, -1), again = run(t, w, h, -1);
    expect(scan.size() == 30, "30 scan frames");
    expect(same(scan, again), "two runs give identical counts");
    expect(!scan.empty() && scan[0].drawn == (uint32_t)(w * h) && scan[0].pushed == (uint32_t)(w * h), "scan frame 0 is a full repaint");
    expect(worstPushed(scan, 2) < (uint32_t)(w * h / 2), "steady scan frames push under half the screen");
    // The board's blips keep the time they were last heard and fade with the clock, so a typical steady frame
    // redraws one sweep wedge (Waveshare 19,888 px, CYD 6-7k px in the probe logs), not the whole dial.
    std::vector<uint32_t> d;
    for (size_t i = 2; i < scan.size(); i++) d.push_back(scan[i].drawn);
    std::sort(d.begin(), d.end());
    printf("    steady scan median drawn %u\n", d.empty() ? 0u : d[d.size() / 2]);
    expect(!d.empty() && d[d.size() / 2] < (uint32_t)(w * h / 6), "a typical steady scan frame draws a wedge, not the whole dial");

    // The built-in HIGH card is the whole screen on both boards, and the alert's first frame draws it.
    const auto high = run(t, w, h, 2);
    expect(worstDrawn(high) >= (uint32_t)(w * h), "alert HIGH draws at least the card");

    // On the board an alert rises over a drawn scan screen: the replay runs scan frames first and returns only the
    // alert's frames, so the alert's frame 0 is not the entry repaint of the whole screen.
    const auto low = run(t, w, h, 0);
    expect(low.size() == 30, "30 alert frames (the scan warm-up is not returned)");
    expect(!low.empty() && low[0].pushed < (uint32_t)(w * h), "alert LOW frame 0 rises over the scan screen (no full repaint)");
}

int main() {
    char tmpl[] = "/tmp/uvmeterXXXXXX";
    root = mkdtemp(tmpl);
    pal::clearPacks();

    // prevws::render must not be disturbed by a meter run (no shared statics)
    std::vector<uint16_t> before(prevws::W * prevws::H), after(prevws::W * prevws::H);
    prevws::render(pal::TH_UMBRAVOX, prevws::SCAN, 5, -1, -1, 0, before.data());
    board(480, 320, "waveshare");
    board(320, 240, "cyd-engine");
    prevws::render(pal::TH_UMBRAVOX, prevws::SCAN, 5, -1, -1, 0, after.data());
    expect(before == after, "prevws::render is unchanged by meter runs");

    // An effect makes the MED alert push more than the same layout without it.
    const char* fx = "{\"format\":1,\"waveshare\":{\"count.wifi\":{\"rect\":[16,52,100,68],\"effect\":\"flash\",\"speed\":\"fast\"},"
                     "\"count.ble\":{\"rect\":[16,196,100,68],\"effect\":\"pulse\",\"speed\":\"fast\"}}}";
    const char* nofx = "{\"format\":1,\"waveshare\":{\"count.wifi\":{\"rect\":[16,52,100,68]},\"count.ble\":{\"rect\":[16,196,100,68]}}}";
    const int a = pack("meter-fx", "METER FX", fx), b = pack("meter-nofx", "METER NOFX", nofx);
    expect(a >= 0 && b >= 0, "both test packs load");
    if (a >= 0 && b >= 0) {
        const auto wf = run(a, 480, 320, 1), nf = run(b, 480, 320, 1);
        printf("    MED worst pushed: effect %u, none %u\n", worstPushed(wf, 1), worstPushed(nf, 1));
        expect(worstPushed(wf, 1) > worstPushed(nf, 1), "effects raise the alert's worst pushed area");
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
