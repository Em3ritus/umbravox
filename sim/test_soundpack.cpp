// Host test: alert.snd, the optional file of a theme pack that replaces the built-in alert beeps.
//   format:  'U','V','S','N', version 1, 0, rate (u16 LE, 16000), three u32 LE sample counts (LOW, MED, HIGH; each 0..48000),
//            then the 16-bit little-endian mono samples of LOW, MED, HIGH back to back. At least one count is non-zero;
//            a level with count 0 keeps the built-in beep.
// A pack is untrusted data, so this is built under the address and UB sanitizers and ends with a mutation fuzz whose
// assertion is an invariant, not mere survival.
// Build: make -C sim test_soundpack
#include "../ui/themepack.h"
#include "../ui/palette.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace themepack;

static int fails = 0;
static void check(const char* what, bool ok) { printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what); if (!ok) fails++; }

// 16-byte aligned like the device's heap buffer, so int16_t reads are aligned.
struct Buf { std::vector<uint8_t> v; const uint8_t* data() const { return v.data(); } size_t size() const { return v.size(); } };
static Buf makeSnd(uint32_t lo, uint32_t me, uint32_t hi) {
    Buf b; b.v.assign(SND_HDR + 2 * (size_t)(lo + me + hi), 0);
    b.v[0] = 'U'; b.v[1] = 'V'; b.v[2] = 'S'; b.v[3] = 'N'; b.v[4] = 1; b.v[6] = 16000 & 255; b.v[7] = 16000 >> 8;
    const uint32_t n[3] = { lo, me, hi };
    for (int k = 0; k < 3; k++) for (int j = 0; j < 4; j++) b.v[8 + 4 * k + j] = (uint8_t)(n[k] >> (8 * j));
    for (size_t i = SND_HDR; i < b.v.size(); i++) b.v[i] = (uint8_t)(i * 5);
    return b;
}
static bool parse_(const Buf& b, Sound& s, char* why = nullptr, size_t cap = 0) { return parseSound(b.data(), b.size(), s, why, cap); }

int main() {
    puts("== constants");
    check("the header is 20 bytes and the rate 16 kHz", SND_HDR == 20 && SND_RATE == 16000);
    check("three seconds at most per sound", MAX_SND_SAMPLES == 48000);
    check("the biggest file is the header plus three 3-second sounds", MAX_SOUND == 20 + 3 * 48000 * 2u);
    check("two sound sets fit the PSRAM budget with room to spare", MAX_SOUNDS == 2 && MAX_SOUNDS * MAX_SOUND < 700000);

    puts("== parseSound: a good file");
    {
        auto b = makeSnd(100, 0, 250); Sound s; char why[48] = "x";
        check("accepted", parse_(b, s, why, sizeof why) && !why[0]);
        check("counts", s.n[0] == 100 && s.n[1] == 0 && s.n[2] == 250);
        check("each level points into the buffer, in order", s.pcm[0] == (const int16_t*)(b.data() + SND_HDR) && s.pcm[2] == (const int16_t*)(b.data() + SND_HDR + 200));
        check("an empty level has no pointer", s.pcm[1] == nullptr);
        int16_t first; memcpy(&first, b.data() + SND_HDR, 2);
        check("samples are read as they are stored", s.pcm[0][0] == first);
        auto one = makeSnd(1, 0, 0), big = makeSnd(48000, 48000, 48000);
        check("one sample, and the maximum, are fine", parse_(one, s) && parse_(big, s) && big.size() == MAX_SOUND && s.n[2] == 48000);
    }

    puts("== parseSound: refusals, each with a reason");
    {
        Sound s; char why[56];
        auto bad = [&](const char* label, Buf b, const char* word) {
            s.pcm[0] = (const int16_t*)1; why[0] = 0;
            const bool ok = parse_(b, s, why, sizeof why);
            char m[120]; snprintf(m, sizeof m, "%s -> refused, pointers cleared, reason mentions \"%s\" (%s)", label, word, why);
            check(m, !ok && !s.pcm[0] && !s.pcm[1] && !s.pcm[2] && strstr(why, word));
        };
        auto g = makeSnd(10, 10, 10);
        { auto x = g; x.v[0] = 'X'; bad("wrong magic", x, "UVSN"); }
        { auto x = g; x.v[4] = 2; bad("a newer version", x, "version"); }
        { auto x = g; x.v[6] = 0x22; x.v[7] = 0x56; bad("22050 Hz: 16000 is the only rate", x, "16000"); }
        { auto x = g; x.v[5] = 1; bad("a reserved byte set", x, "reserved"); }
        { auto x = g; x.v[8] = 0x81; x.v[9] = 0xBB; x.v[10] = 0; x.v[11] = 0; bad("48001 samples", x, "48000"); }
        { auto x = makeSnd(0, 0, 0); bad("no sounds at all", x, "no sounds"); }
        { auto x = g; x.v.pop_back(); bad("one byte short", x, "length"); }
        { auto x = g; x.v.push_back(0); bad("one byte long", x, "length"); }
        { auto x = g; x.v.resize(10); bad("shorter than a header", x, "short"); }
        { Buf x; bad("empty", x, "short"); }
    }

    puts("== palette: a pack's sounds are kept by pointer, cleared with the pack");
    {
        pal::clearPacks();
        auto b = makeSnd(20, 30, 40); Sound s; parse_(b, s);
        Pack p{}; strcpy(p.name, "SOUND TEST"); strcpy(p.header, "SOUND TEST"); p.centre = CENTRE_RADAR; p.sweeps = 1; p.sweep = 0; p.artOpacity = 100; p.artScale = 82;
        for (int i = 0; i < N_COLORS; i++) p.color[i] = (uint16_t)(0x1111 * (i % 13) + 0x0842);
        const int id = pal::addPack(p, nullptr, nullptr, &s);
        check("registered", id >= pal::TH_COUNT);
        check("pal::sound returns it", pal::sound((uint8_t)id) == &s);
        check("a clip and sounds are independent", pal::clip((uint8_t)id) == nullptr);
        check("built-ins and unknown ids have none", pal::sound(pal::TH_UMBRAVOX) == nullptr && pal::sound(250) == nullptr);
        pal::addPack(p, nullptr);
        check("re-adding the pack without sounds drops them", pal::sound((uint8_t)id) == nullptr);
        pal::addPack(p, nullptr, nullptr, &s);
        pal::clearPacks();
        check("clearPacks forgets the sounds", pal::sound((uint8_t)id) == nullptr);
    }

    puts("== fuzz: 4000 mutations of a good file");
    {
        auto g = makeSnd(60, 0, 90); srand(7); int bad = 0;
        for (int it = 0; it < 4000; it++) {
            auto x = g; const int nm = 1 + rand() % 3;
            for (int k = 0; k < nm; k++) x.v[rand() % SND_HDR] ^= (uint8_t)(1 + rand() % 255);
            if (rand() % 4 == 0) x.v.resize(rand() % (x.v.size() + 4));
            Sound s; char why[56];
            const bool ok = parse_(x, s, why, sizeof why);
            if (ok) {                               // invariant: an accepted file is exactly what its header says, and every pointer is inside it
                uint32_t total = 0; for (int k = 0; k < 3; k++) total += s.n[k];
                if (x.size() != SND_HDR + 2 * (size_t)total || total == 0) bad++;
                for (int k = 0; k < 3; k++) if (s.n[k] && (const uint8_t*)s.pcm[k] + 2 * s.n[k] > x.data() + x.size()) bad++;
            } else if (s.pcm[0] || s.pcm[1] || s.pcm[2]) bad++;
        }
        check("every accepted file matches its header; every refused one leaves no pointer", bad == 0);
    }

    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
