// The beeps for an alert, one pattern per confidence level, as 16-bit mono samples. Pure and host-tested
// (sim/test_alert_tone.cpp); device/sound.cpp plays them through the board's speaker.
//   LOW    one short low beep
//   MED    two higher beeps
//   HIGH   a six-note two-tone siren (the level can be told by ear without looking)
// Integers only (the C5 has no FPU). A sample depends only on its index, so rendering in any chunk size matches one
// pass. Each note ramps in and out over 5 ms so the speaker does not click.
#pragma once
#include <cstdint>

namespace alerttone {

constexpr int kRate = 16000;                       // samples per second
struct Note { uint16_t hz, ms; };                  // hz == 0 is a rest

namespace detail {
constexpr Note kLow[]  = { {1047, 220} };
constexpr Note kMed[]  = { {1175, 110}, {0, 70}, {1175, 110} };
constexpr Note kHigh[] = { {1568, 140}, {988, 140}, {1568, 140}, {988, 140}, {1568, 140}, {988, 140} };
inline int clampLevel(int lv) { return lv < 0 ? 0 : lv > 2 ? 2 : lv; }
inline const Note* notes(int lv, int& n) {
    lv = clampLevel(lv);
    if (lv == 0) { n = (int)(sizeof kLow / sizeof kLow[0]); return kLow; }
    if (lv == 1) { n = (int)(sizeof kMed / sizeof kMed[0]); return kMed; }
    n = (int)(sizeof kHigh / sizeof kHigh[0]); return kHigh;
}
constexpr int kRamp = kRate * 5 / 1000;            // 80 samples
// sin(2*pi*x) for a 32-bit phase, as a parabola in Q15: close enough for a beep.
inline int32_t wave(uint32_t phase) {
    const int32_t x = (int32_t)phase >> 16;        // -32768 .. 32767
    const int32_t ax = x < 0 ? -x : x;
    int32_t y = (x * (32768 - ax)) >> 13;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}
} // namespace detail

inline int noteCount(int lv) { int n; const Note* t = detail::notes(lv, n); int c = 0; for (int i = 0; i < n; i++) c += t[i].hz != 0; return c; }
inline int firstHz(int lv) { int n; const Note* t = detail::notes(lv, n); for (int i = 0; i < n; i++) if (t[i].hz) return t[i].hz; return 0; }
inline int totalSamples(int lv) { int n; const Note* t = detail::notes(lv, n); int s = 0; for (int i = 0; i < n; i++) s += t[i].ms * kRate / 1000; return s; }

// Samples [start, start+n) of the level's beep into out; returns how many were written (0 once the beep is over).
// amp is the peak, 0..32767.
inline int render(int lv, int start, int16_t* out, int n, int amp) {
    int cnt; const Note* t = detail::notes(lv, cnt);
    const int total = totalSamples(lv);
    if (start < 0 || start >= total || n <= 0) return 0;
    if (n > total - start) n = total - start;
    int begin = 0, ni = 0;
    while (ni < cnt && start >= begin + t[ni].ms * kRate / 1000) { begin += t[ni].ms * kRate / 1000; ni++; }
    int i = start;
    for (int w = 0; w < n; w++, i++) {
        while (i >= begin + t[ni].ms * kRate / 1000) { begin += t[ni].ms * kRate / 1000; ni++; }
        const int len = t[ni].ms * kRate / 1000, k = i - begin;
        if (!t[ni].hz) { out[w] = 0; continue; }
        const uint32_t step = (uint32_t)(((uint64_t)t[ni].hz << 32) / kRate);
        int e = k < len - 1 - k ? k : len - 1 - k;                      // distance to the nearer edge
        if (e > detail::kRamp) e = detail::kRamp;
        const int32_t env = e * 256 / detail::kRamp;                    // 0..256
        const int32_t s = (detail::wave((uint32_t)k * step) * amp) >> 15;
        out[w] = (int16_t)((s * env) >> 8);
    }
    return n;
}

} // namespace alerttone
