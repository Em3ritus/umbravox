// Host tests for the settings a theme switch introduces.
//
// The comparator is the case that matters. It lives here rather than in
// device/prefs.cpp because prefs.cpp pulls in Arduino.h and cannot be built
// on a host -- and a comparator that silently forgets a field stops the save
// timer from ever firing, which is a bug this project has already had once.
#include "../ui/nav.h"
#include "../ui/palette.h"
#include "../ui/themepack.h"
#include <stdio.h>
#include <string.h>
#include <new>

static int fails = 0;

static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

// A hand-built pack (no parser needed): status colours that pass the contrast
// check, and the caller's sweep offering and starting sweep.
static themepack::Pack mkPack(const char* name, uint16_t sweeps, uint8_t sweep) {
    themepack::Pack p;
    memset(&p, 0, sizeof p);
    snprintf(p.name, sizeof p.name, "%s", name);
    snprintf(p.header, sizeof p.header, "%s", name);
    p.dial = themepack::DIAL_COMPACT;
    p.centre = themepack::CENTRE_HEPTAGRAM;
    p.sweeps = sweeps;
    p.sweep = sweep;
    for (int i = 0; i < themepack::N_COLORS; i++) p.color[i] = (uint16_t)(0x2104 * (i + 1));
    p.color[themepack::R_VOID] = 0x0000;
    p.color[themepack::R_AMBER] = 0xFFE0;
    p.color[themepack::R_RED] = 0xF800;
    return p;
}

int main() {
    using namespace ui;

    // Two packs, so every theme loop below reaches past the built-ins: one
    // that offers every sweep, and one that omits RADAR and starts on STAR.
    const int fullId = pal::addPack(mkPack("FULL", 0x07FF, SW_RADAR), nullptr);
    const int noRadarId = pal::addPack(
        mkPack("NORADAR", (uint16_t)((1u << SW_STAR) | (1u << SW_PULSE)), SW_STAR), nullptr);
    check("packs loaded for the loops", fullId == 3 && noRadarId == 4 && pal::themeCount() == 5);

    // Adding a field to Settings must not quietly widen what this test treats
    // as padding. A byte the constructor never writes is indistinguishable
    // from padding by the heuristic below -- which is true of real padding,
    // and equally true of a new field someone declared without a default
    // member initializer. That field would be skipped, and skipping a field
    // is the exact failure this test exists to catch.
    //
    // So: grow the struct and this assert fires. Give every new field a
    // default initializer, then update the size here deliberately.
    static_assert(sizeof(Settings) == 12,
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
        // Settings has no padding (the `labels` member fills the slack
        // byte), so every byte the constructor leaves untouched is a field
        // someone forgot to initialise. Zero is what makes this heuristic
        // exact rather than approximate: with any slack, such a field could
        // hide in it with sizeof and the count unchanged.
        int padBytes = 0;
        for (size_t off = 0; off < sizeof(Settings); off++)
            if (z[off] != f[off]) padBytes++;
        check("no padding, no uninitialised field", padBytes == 0);

        int missed = 0;
        for (int th = 0; th < pal::themeCount(); th++) {
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
        b = a; b.labels = 1;
        check("a label-mode change is detected", !settingsEqual(a, b));
        b = a; b.alertStyle = AL_VORTEX;
        check("an alert-style change is detected", !settingsEqual(a, b));
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

        s = Settings();
        s.alertStyle = 200;
        clampSettings(s);
        check("out-of-range alert style falls back", s.alertStyle < ALERT_STYLE_COUNT);
    }

    printf("a sweep must be available on its theme:\n");
    {
        Settings s;
        s.theme = pal::TH_BAPH0MAU5;
        s.sweep = SW_GAZE;
        clampSettings(s);
        check("gaze survives on baph0mau5", s.sweep == SW_GAZE);

        s.theme = pal::TH_UMBRAVOX;        // the gaze has no eyes here
        clampSettings(s);
        check("gaze falls back to radar on umbravox", s.sweep == SW_RADAR);

        s.sweep = SW_STAR;
        clampSettings(s);
        check("star is kept on umbravox", s.sweep == SW_STAR);
    }

    printf("pack themes clamp against the loaded table:\n");
    {
        Settings s;
        s.theme = (uint8_t)fullId;
        s.sweep = SW_GAZE;
        clampSettings(s);
        check("a loaded pack id is kept", s.theme == fullId && s.sweep == SW_GAZE);

        s = Settings();
        s.theme = (uint8_t)(pal::themeCount() - 1);
        clampSettings(s);
        check("the last pack id is kept", s.theme == pal::themeCount() - 1);

        s = Settings();
        s.theme = (uint8_t)pal::themeCount();       // one past the last pack
        clampSettings(s);
        check("an id past the packs snaps to UMBRAVOX", s.theme == pal::TH_UMBRAVOX);

        s = Settings();
        s.theme = (uint8_t)noRadarId;               // default sweep is RADAR
        clampSettings(s);
        check("a pack without radar falls back to its own start sweep", s.sweep == SW_STAR);

        s.sweep = SW_GAZE;                          // withdrawn by this pack
        clampSettings(s);
        check("a withdrawn sweep falls back to the pack's start", s.sweep == SW_STAR);

        s.sweep = SW_PULSE;                         // offered: kept
        clampSettings(s);
        check("an offered sweep is kept on the pack", s.sweep == SW_PULSE);
    }

    printf("the mask matches the enum:\n");
    {
        check("umbravox offers radar",  pal::sweepMask(pal::TH_UMBRAVOX) & (1 << SW_RADAR));
        check("umbravox refuses gaze", !(pal::sweepMask(pal::TH_UMBRAVOX) & (1 << SW_GAZE)));
        check("baph0mau5 offers gaze",    pal::sweepMask(pal::TH_BAPH0MAU5) & (1 << SW_GAZE));
        for (int s = 0; s < SW_COUNT; s++)
            check("baph0mau5 offers every sweep",
                  pal::sweepMask(pal::TH_BAPH0MAU5) & (1 << s));
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
