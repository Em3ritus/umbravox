// Host tests for the settings a theme switch introduces.
//
// The comparator is the case that matters. It lives here rather than in
// device/prefs.cpp because prefs.cpp pulls in Arduino.h and cannot be built
// on a host -- and a comparator that silently forgets a field stops the save
// timer from ever firing, which is a bug this project has already had once.
#include "../ui/nav.h"
#include "../ui/palette.h"
#include <stdio.h>
#include <string.h>
#include <new>

static int fails = 0;

static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

int main() {
    using namespace ui;

    // Adding a field to Settings must not quietly widen what this test treats
    // as padding. A byte the constructor never writes is indistinguishable
    // from padding by the heuristic below -- which is true of real padding,
    // and equally true of a new field someone declared without a default
    // member initializer. That field would be skipped, and skipping a field
    // is the exact failure this test exists to catch.
    //
    // So: grow the struct and this assert fires. Give every new field a
    // default initializer, then update the size here deliberately.
    static_assert(sizeof(Settings) == 10,
                  "Settings changed size -- see the padding note above, and "
                  "confirm every field has a default member initializer");

    printf("equality covers every field:\n");
    {
        // Walk the struct a byte at a time. A field added later but left out
        // of settingsEqual() is caught here without anyone remembering to
        // add a case, which is the only way this stays true.
        // Padding is not a field. Build two Settings over buffers that start
        // as 0x00 and 0xFF: default-initialising writes every member but
        // leaves padding alone, so any byte that still differs is padding.
        alignas(Settings) unsigned char z[sizeof(Settings)], f[sizeof(Settings)];
        memset(z, 0x00, sizeof z);
        memset(f, 0xFF, sizeof f);
        new (z) Settings;
        new (f) Settings;

        // Clamping can pull a mutated byte straight back (a flipped theme is
        // out of range), so one mutation per byte proves little. Try several
        // masks on every theme's base, require that any mutation which
        // survives the clamp is noticed, and that every real byte had at
        // least one mutation survive -- otherwise it was never exercised.
        static const unsigned char MASKS[] = {0xFF, 0x01, 0x02, 0x04, 0x10};
        bool exercised[sizeof(Settings)] = {};
        // Settings has no padding (the `reserved` member fills the slack
        // byte), so every byte the constructor leaves untouched is a field
        // someone forgot to initialise. Zero is what makes this heuristic
        // exact rather than approximate: with any slack, such a field could
        // hide in it with sizeof and the count unchanged.
        int padBytes = 0;
        for (size_t off = 0; off < sizeof(Settings); off++)
            if (z[off] != f[off]) padBytes++;
        check("no padding, no uninitialised field", padBytes == 0);

        int missed = 0;
        for (int th = 0; th < pal::TH_COUNT; th++) {
            Settings base;
            base.theme = (uint8_t)th;
            clampSettings(base);
            for (size_t off = 0; off < sizeof(Settings); off++) {
                if (z[off] != f[off]) continue;     // padding, not a field
                for (unsigned char m : MASKS) {
                    Settings a = base, b = base;
                    reinterpret_cast<unsigned char*>(&b)[off] ^= m;
                    clampSettings(b);
                    if (memcmp(&a, &b, sizeof(Settings)) == 0) continue;  // clamped back
                    exercised[off] = true;
                    if (settingsEqual(a, b)) missed++;    // differs, yet "equal"
                }
            }
        }
        int unexercised = 0;
        for (size_t off = 0; off < sizeof(Settings); off++)
            if (z[off] == f[off] && !exercised[off]) unexercised++;
        check("no field differs without settingsEqual noticing", missed == 0);
        check("every field byte was actually exercised", unexercised == 0);
    }

    printf("equality is reflexive and specific:\n");
    {
        Settings a, b;
        check("identical settings compare equal", settingsEqual(a, b));
        b.theme = pal::TH_BAPH0MAU5;
        check("a theme change is detected", !settingsEqual(a, b));
        b = a; b.sweep = SW_STAR;
        check("a sweep change is detected", !settingsEqual(a, b));
        b = a; b.bright = (uint8_t)(a.bright + 1);
        check("an existing field is still detected", !settingsEqual(a, b));
    }

    printf("clamping survives a corrupt store:\n");
    {
        Settings s;
        s.theme = 200;
        clampSettings(s);
        check("out-of-range theme falls back", s.theme < pal::TH_COUNT);

        s = Settings();
        s.sweep = 200;
        clampSettings(s);
        check("out-of-range sweep falls back", s.sweep < SW_COUNT);
    }

    printf("a sweep must be available on its theme:\n");
    {
        Settings s;
        s.theme = pal::TH_BAPH0MAU5;
        s.sweep = SW_GAZE;
        clampSettings(s);
        check("gaze survives on baph0mau5", s.sweep == SW_GAZE);

        s.theme = pal::TH_AETHERSCAN;        // the gaze has no eyes here
        clampSettings(s);
        check("gaze falls back to radar on aetherscan", s.sweep == SW_RADAR);

        s.sweep = SW_STAR;
        clampSettings(s);
        check("star is kept on aetherscan", s.sweep == SW_STAR);
    }

    printf("the mask matches the enum:\n");
    {
        check("aetherscan offers radar",  pal::sweepMask(pal::TH_AETHERSCAN) & (1 << SW_RADAR));
        check("aetherscan refuses gaze", !(pal::sweepMask(pal::TH_AETHERSCAN) & (1 << SW_GAZE)));
        check("baph0mau5 offers gaze",    pal::sweepMask(pal::TH_BAPH0MAU5) & (1 << SW_GAZE));
        for (int s = 0; s < SW_COUNT; s++)
            check("baph0mau5 offers every sweep",
                  pal::sweepMask(pal::TH_BAPH0MAU5) & (1 << s));
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
