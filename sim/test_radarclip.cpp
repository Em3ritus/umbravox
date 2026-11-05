// Host test: radar.vid, the optional third file of a theme pack (a looping clip for the wide radar).
//   format:  'U','V','R','V', version 1, w 102, h 102, fps 1..30, frames (u16 LE) 1..150, 0, 0, then frames x 5202 bytes of
//            4-bit grey (two pixels per byte, high nibble first).
// A pack is untrusted data, so this is built under the address and UB sanitizers and ends with a mutation fuzz whose
// assertion is an invariant, not mere survival.
// Build: make -C sim test_radarclip
#include "../ui/themepack.h"
#include "../ui/palette.h"
#include "../ui/radar_clip.h"
#include "packload.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
using namespace themepack;

static int fails = 0;
static void check(const char* what, bool ok) { printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what); if (!ok) fails++; }

static std::vector<uint8_t> makeClip(int frames, int fps = 12, int w = 102, int h = 102) {
    std::vector<uint8_t> v(CLIP_HDR + (size_t)frames * CLIP_FRAME_BYTES, 0);
    v[0] = 'U'; v[1] = 'V'; v[2] = 'R'; v[3] = 'V'; v[4] = 1; v[5] = (uint8_t)w; v[6] = (uint8_t)h; v[7] = (uint8_t)fps;
    v[8] = (uint8_t)(frames & 255); v[9] = (uint8_t)(frames >> 8);
    for (size_t i = CLIP_HDR; i < v.size(); i++) v[i] = (uint8_t)(i * 7);
    return v;
}
static bool parse_(const std::vector<uint8_t>& v, Clip& c, char* why = nullptr, size_t cap = 0) { return parseClip(v.data(), v.size(), c, why, cap); }

int main() {
    puts("== constants");
    check("a frame is 5202 bytes", CLIP_FRAME_BYTES == 5202);
    check("the biggest file is 12 + 150 frames", MAX_CLIP == 12 + 150 * 5202u);
    check("two clips fit the PSRAM budget with room to spare", MAX_CLIPS == 2 && MAX_CLIPS * MAX_CLIP < 1700000);

    puts("== parseClip: a good clip");
    {
        auto v = makeClip(3, 12); Clip c; char why[48] = "x";
        check("accepted", parse_(v, c, why, sizeof why) && !why[0]);
        check("shape", c.w == 102 && c.h == 102 && c.fps == 12 && c.frames == 3);
        check("pixels point just past the header", c.px == v.data() + CLIP_HDR);
        auto one = makeClip(1, 1); auto big = makeClip(MAX_CLIP_FRAMES, 30);
        check("one frame at 1 fps and the maximum at 30 fps are fine", parse_(one, c) && parse_(big, c) && c.frames == 150 && big.size() == MAX_CLIP);
    }

    puts("== parseClip: refusals, each with a reason");
    {
        Clip c; char why[48];
        auto bad = [&](const char* label, std::vector<uint8_t> v, const char* word) {
            c.px = (const uint8_t*)1; why[0] = 0;
            const bool ok = parse_(v, c, why, sizeof why);
            char m[96]; snprintf(m, sizeof m, "%s -> refused, px cleared, reason mentions \"%s\" (%s)", label, word, why);
            check(m, !ok && c.px == nullptr && strstr(why, word));
        };
        auto v = makeClip(2);
        { auto x = v; x[0] = 'X'; bad("wrong magic", x, "UVRV"); }
        { auto x = v; x[4] = 2; bad("a newer version", x, "version"); }
        { auto x = v; x[5] = 64; bad("width 64", x, "102"); }
        { auto x = v; x[6] = 101; bad("height 101", x, "102"); }
        { auto x = v; x[7] = 0; bad("fps 0", x, "fps"); }
        { auto x = v; x[7] = 31; bad("fps 31", x, "fps"); }
        { auto x = v; x[8] = 0; x[9] = 0; bad("zero frames", x, "frames"); }
        { auto x = makeClip(1); x[8] = 151 & 255; x[9] = 0; x.resize(CLIP_HDR + 151 * CLIP_FRAME_BYTES); bad("151 frames", x, "frames"); }
        { auto x = v; x[10] = 1; bad("reserved byte set", x, "reserved"); }
        { auto x = v; x.pop_back(); bad("one byte short", x, "length"); }
        { auto x = v; x.push_back(0); bad("one byte long", x, "length"); }
        { std::vector<uint8_t> x(5, 0); bad("shorter than the header", x, "short"); }
        { std::vector<uint8_t> x; c.px = (const uint8_t*)1; check("empty input, null data", !parseClip(nullptr, 0, c, why, sizeof why) && c.px == nullptr); }
        check("a null why buffer is fine", !parseClip(v.data(), 3, c, nullptr, 0));
    }

    puts("== palette: a pack's clip is kept by pointer, cleared with the pack");
    {
        pal::clearPacks();
        auto v = makeClip(2); Clip c; parse_(v, c);
        Pack p{}; strcpy(p.name, "CLIP TEST"); strcpy(p.header, "CLIP TEST"); p.centre = CENTRE_RADAR; p.sweeps = 1; p.sweep = 0; p.artOpacity = 100; p.artScale = 82;
        for (int i = 0; i < N_COLORS; i++) p.color[i] = (uint16_t)(0x1111 * (i % 13) + 0x0842);
        const int id = pal::addPack(p, nullptr, &c);
        check("registered", id >= pal::TH_COUNT);
        check("pal::clip returns it", pal::clip((uint8_t)id) == &c);
        check("built-ins have none", pal::clip(pal::TH_UMBRAVOX) == nullptr && pal::clip(pal::TH_BAPH0MAU5) == nullptr);
        check("an unknown id has none", pal::clip(250) == nullptr);
        const radarclip::Video* rv = radarclip::videoFor((uint8_t)id);
        check("videoFor mirrors it for the renderer", rv && rv->w == 102 && rv->h == 102 && rv->frames == 2 && rv->fps == 12 && rv->px == c.px);
        check("videoFor is null for a theme without a clip", radarclip::videoFor(pal::TH_UMBRAVOX) == nullptr);
        check("the table is small (internal RAM is scarce): one entry per possible theme, not per byte value",
              radarclip::kSlots == pal::TH_COUNT + themepack::MAX_PACKS && sizeof(radarclip::Video) * radarclip::kSlots < 700);
        check("an id past the table has no clip, and does not read outside it", radarclip::videoFor((uint8_t)radarclip::kSlots) == nullptr && radarclip::videoFor(255) == nullptr);
        pal::addPack(p, nullptr);                                         // the same name again, no clip: it replaces the slot
        check("re-adding the pack without a clip drops it", pal::clip((uint8_t)id) == nullptr && radarclip::videoFor((uint8_t)id) == nullptr);
        pal::addPack(p, nullptr, &c);
        pal::clearPacks();
        check("clearPacks forgets the clip", pal::clip((uint8_t)id) == nullptr);
    }

    puts("== host loader: radar.vid in a pack folder");
    {
        char dir[] = "/tmp/uvx-clip-XXXXXX"; if (!mkdtemp(dir)) return 2;
        auto w = [&](const char* sub, const char* file, const void* d, size_t n) {
            std::string pth = std::string(dir) + "/" + sub; mkdir(pth.c_str(), 0755);
            FILE* f = fopen((pth + "/" + file).c_str(), "wb"); fwrite(d, 1, n, f); fclose(f);
        };
        const std::string json = "{\"format\":1,\"name\":\"CLIP PACK\",\"header\":\"CLIP PACK\",\"dial\":\"large\",\"centrepiece\":\"radar\","
            "\"sweeps\":[\"radar\"],\"sweep\":\"radar\",\"alert\":\"ward\",\"colors\":{"
            "\"void\":\"#09071C\",\"panel\":\"#16103A\",\"grid\":\"#1E1646\",\"edge\":\"#3A2A6E\",\"edgeLit\":\"#A84DFF\",\"dim\":\"#8A7BD0\","
            "\"text\":\"#E8DEFF\",\"bright\":\"#FFFFFF\",\"witch\":\"#A84DFF\",\"witchHi\":\"#FF1FCC\",\"bone\":\"#FFD04A\",\"sweep\":\"#1EF5EE\","
            "\"sweepHi\":\"#FF1FCC\",\"live\":\"#FF58B0\",\"liveHi\":\"#FFF0D0\",\"magenta\":\"#FF1FCC\",\"ice\":\"#1EF5EE\",\"green\":\"#3BFF9E\","
            "\"amber\":\"#FFC24A\",\"red\":\"#FF4D6A\"}}";
        char why[96];
        auto good = makeClip(4);
        w("clippack", "theme.json", json.data(), json.size()); w("clippack", "radar.vid", good.data(), good.size());
        pal::clearPacks();
        const int id = packload::load((std::string(dir) + "/clippack").c_str(), why, sizeof why);
        char m[160]; snprintf(m, sizeof m, "a pack with radar.vid loads (%s)", why); check(m, id >= pal::TH_COUNT);
        const Clip* c = id >= 0 ? pal::clip((uint8_t)id) : nullptr;
        check("its clip is registered, 4 frames, bytes intact", c && c->frames == 4 && c->px && !memcmp(c->px, good.data() + CLIP_HDR, 4 * CLIP_FRAME_BYTES));

        pal::clearPacks();
        w("nopack", "theme.json", json.data(), json.size());
        const int id2 = packload::load((std::string(dir) + "/nopack").c_str(), why, sizeof why);
        check("a pack without a clip loads and has none", id2 >= pal::TH_COUNT && pal::clip((uint8_t)id2) == nullptr);

        pal::clearPacks();
        auto broken = good; broken[5] = 50;
        w("badpack", "theme.json", json.data(), json.size()); w("badpack", "radar.vid", broken.data(), broken.size());
        const int id3 = packload::load((std::string(dir) + "/badpack").c_str(), why, sizeof why);
        snprintf(m, sizeof m, "a broken radar.vid skips the whole pack, with the file named (%s)", why);
        check(m, id3 < 0 && strstr(why, "radar.vid") && pal::themeCount() == pal::TH_COUNT);

        pal::clearPacks();
        auto huge = std::vector<uint8_t>(MAX_CLIP + 1, 0);
        w("hugepack", "theme.json", json.data(), json.size()); w("hugepack", "radar.vid", huge.data(), huge.size());
        const int id4 = packload::load((std::string(dir) + "/hugepack").c_str(), why, sizeof why);
        snprintf(m, sizeof m, "an over-size radar.vid is refused before it is read (%s)", why);
        check(m, id4 < 0 && strstr(why, "radar.vid") && strstr(why, "780312"));

        pal::clearPacks();
        packload::detail::clipsUsed() = 0;                              // clearPacks forgot the earlier packs; so does their budget
        int loaded = 0;
        for (int k = 0; k < 3; k++) {
            char nm[16]; snprintf(nm, sizeof nm, "p%d", k); const std::string title = std::string("CLIP ") + char('A' + k);
            std::string js = json; js.replace(js.find("CLIP PACK"), 9, title); js.replace(js.find("CLIP PACK"), 9, title);
            w(nm, "theme.json", js.data(), js.size()); w(nm, "radar.vid", good.data(), good.size());
            const int idk = packload::load((std::string(dir) + "/" + nm).c_str(), why, sizeof why);
            if (idk >= 0) loaded++;
            if (k == 2) { snprintf(m, sizeof m, "the third clip is refused: only %d fit (%s)", MAX_CLIPS, why); check(m, idk < 0 && strstr(why, "clip")); }
        }
        check("two clips loaded before that", loaded == MAX_CLIPS);
        pal::clearPacks();
    }

    puts("== fuzz: mutations never yield a clip that reads outside its buffer");
    {
        srand(12345);
        auto base = makeClip(5);
        int accepted = 0, refused = 0; bool inv = true;
        for (int it = 0; it < 4000 && inv; it++) {
            auto v = base;
            const int flips = 1 + rand() % 4;
            for (int k = 0; k < flips; k++) v[rand() % CLIP_HDR] ^= (uint8_t)(1 << (rand() % 8));
            if (rand() % 5 == 0) v.resize(rand() % (v.size() + 8));
            Clip c; c.px = (const uint8_t*)1;
            if (parseClip(v.data(), v.size(), c, nullptr, 0)) {
                accepted++;
                inv = c.px == v.data() + CLIP_HDR && c.frames >= 1 && c.frames <= MAX_CLIP_FRAMES && c.w == 102 && c.h == 102
                      && c.fps >= 1 && c.fps <= 30 && CLIP_HDR + (size_t)c.frames * CLIP_FRAME_BYTES == v.size();
            } else { refused++; inv = c.px == nullptr; }
        }
        char m[96]; snprintf(m, sizeof m, "invariant held over 4000 mutations (%d accepted, %d refused)", accepted, refused);
        check(m, inv && refused > 100);
    }

    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
