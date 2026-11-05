// Host test: the alert beeps (one pattern per confidence level), as 16-bit mono samples at 16 kHz.
//   LOW is one short low beep, MED two, HIGH a six-note rising-falling siren: a different count and length each,
//   so the level can be told by ear. Samples are made with integers only (the C5 has no FPU), each note ramps in and
//   out so the speaker does not click, and rendering in any chunk size gives the same samples as one pass.
// Build: make -C sim test_alert_tone
#include "../ui/alert_tone.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace alerttone;

static int fails = 0;
static void expect(bool c, const char* m) { if (!c) { ++fails; printf("  FAIL %s\n", m); } else printf("  ok   %s\n", m); }

static std::vector<int16_t> all(int level, int amp) {
    std::vector<int16_t> v(totalSamples(level));
    const int n = render(level, 0, v.data(), (int)v.size(), amp);
    v.resize(n); return v;
}

int main() {
    puts("== shapes: each level is a different count and length");
    expect(kRate == 16000, "16 kHz");
    expect(noteCount(0) == 1 && noteCount(1) == 2 && noteCount(2) == 6, "LOW 1 beep, MED 2, HIGH 6");
    expect(totalSamples(0) < totalSamples(1) && totalSamples(1) < totalSamples(2), "longer with the level");
    expect(totalSamples(0) == 220 * 16, "LOW is 220 ms");
    expect(noteCount(9) == noteCount(2) && totalSamples(-3) == totalSamples(0), "an out-of-range level clamps");
    expect(firstHz(0) != firstHz(1) || firstHz(1) != firstHz(2), "not the same pitch for every level");

    puts("== rendering");
    for (int lv = 0; lv < 3; lv++) {
        const auto a = all(lv, 12000);
        int peak = 0; for (int16_t s : a) peak = std::max(peak, std::abs((int)s));
        char m[80];
        snprintf(m, sizeof m, "level %d: full length, peak %d within the amplitude and above half of it", lv, peak);
        expect((int)a.size() == totalSamples(lv) && peak <= 12000 && peak > 6000, m);
        expect(render(lv, totalSamples(lv), nullptr, 100, 12000) == 0, "nothing after the end");
    }
    {
        const auto a = all(0, 12000);                       // LOW: edges are quiet (ramp), middle is loud
        expect(std::abs(a[0]) < 400 && std::abs(a[a.size() - 1]) < 400, "a note starts and ends near zero (no click)");
    }
    {
        const auto m = all(1, 12000);                       // MED: 110 ms beep, 70 ms gap, 110 ms beep
        bool gap = true; for (int i = 110 * 16; i < 180 * 16; i++) gap = gap && m[i] == 0;
        expect(gap, "the gap between beeps is silent");
        int loud = 0; for (int i = 20 * 16; i < 90 * 16; i++) loud = std::max(loud, std::abs((int)m[i]));
        expect(loud > 6000, "the beep itself is loud");
    }
    {
        const auto a = all(0, 12000);                       // pitch: zero crossings in the steady part of the LOW beep
        int zc = 0; const int s0 = 20 * 16, s1 = 100 * 16;
        for (int i = s0 + 1; i < s1; i++) zc += (a[i - 1] < 0) != (a[i] < 0);
        const double hz = zc / 2.0 / ((s1 - s0) / 16000.0);
        expect(hz > firstHz(0) * 0.97 && hz < firstHz(0) * 1.03, "the pitch is the note's pitch (+-3%)");
    }
    {
        const auto m = all(1, 12000);                       // the same low volume scales the samples
        const auto q = all(1, 3000);
        int pm = 0, pq = 0; for (size_t i = 0; i < m.size(); i++) { pm = std::max(pm, std::abs((int)m[i])); pq = std::max(pq, std::abs((int)q[i])); }
        expect(pq * 3 < pm && pq > 500, "amplitude scales the beep");
    }

    puts("== chunked rendering equals one pass");
    for (int lv = 0; lv < 3; lv++) {
        const auto ref = all(lv, 9000);
        for (int chunk : {1, 7, 160, 333, 4096}) {
            std::vector<int16_t> got; std::vector<int16_t> buf(chunk);
            for (int at = 0;;) { const int n = render(lv, at, buf.data(), chunk, 9000); if (n <= 0) break; got.insert(got.end(), buf.begin(), buf.begin() + n); at += n; }
            char m[80]; snprintf(m, sizeof m, "level %d, chunk %d", lv, chunk);
            expect(got == ref, m);
        }
    }

    puts("== a pack's own sound (16-bit samples) through the same volume steps");
    {
        std::vector<int16_t> src(1000);
        for (int i = 0; i < 1000; i++) src[i] = (int16_t)((i % 50) * 400 - 10000);          // a saw, -10000..9600
        auto run = [&](int amp) { std::vector<int16_t> o(1000); int n = renderPcm(src.data(), 1000, 0, o.data(), 1000, amp); o.resize(n); return o; };
        const auto unity = run(kNormalAmp), quiet = run(kNormalAmp / 4), loud = run(kNormalAmp * 2);
        expect(unity.size() == 1000 && run(kNormalAmp).size() == 1000, "the whole sound comes out");
        expect(unity[500] == src[500], "NORMAL plays the samples as they are");
        expect(quiet[500] == src[500] / 4 || std::abs(quiet[500] - src[500] / 4) <= 1, "a quarter of the amplitude is a quarter of the samples");
        expect(std::abs(loud[500] - 2 * src[500]) <= 1, "twice the amplitude doubles them");
        std::vector<int16_t> hot(1000, 30000), o(1000); renderPcm(hot.data(), 1000, 0, o.data(), 1000, kNormalAmp * 3);
        expect(o[500] == 32767, "a hot sample clips at full scale instead of wrapping");
        expect(std::abs(unity[0]) < 400 && std::abs(unity[999]) < 400, "the edges ramp so the speaker does not click");
        expect(renderPcm(src.data(), 1000, 1000, nullptr, 50, kNormalAmp) == 0 && renderPcm(nullptr, 0, 0, nullptr, 50, kNormalAmp) == 0, "nothing after the end, or for no sound");
        std::vector<int16_t> got; int16_t buf[7];
        for (int at = 0;;) { const int n = renderPcm(src.data(), 1000, at, buf, 7, kNormalAmp); if (n <= 0) break; got.insert(got.end(), buf, buf + n); at += n; }
        expect(got == unity, "chunked rendering equals one pass");
        expect(kNormalAmp == 12000, "NORMAL is the tone's own volume, so custom and built-in sounds match");
    }

    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
