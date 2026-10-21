// Host tests for the theme table: a theme is a row, and adding one must not
// break the fallback or the sweep offering.
#include "../ui/palette.h"
#include "../ui/layout.h"
#include "../ui/model.h"
#include "../ui/muffin.h"
#include "../ui/muffinseal.h"
#include "../ui/themepack.h"
#include <cstring>
#include <cstdio>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

// A pack built by hand rather than parsed: every colour distinct (base + role
// index), a status pair that passes the contrast check against a black void,
// and the caller's dial, sweeps and starting sweep.
static themepack::Pack mkPack(const char* name, themepack::Dial dial, uint16_t sweeps,
                              uint8_t sweep, uint16_t base) {
    themepack::Pack p;
    memset(&p, 0, sizeof p);
    snprintf(p.name, sizeof p.name, "%s", name);
    snprintf(p.header, sizeof p.header, "HDR %s", name);
    p.dial = dial;
    p.centre = themepack::CENTRE_HEPTAGRAM;
    p.rite = themepack::RITE_COMET;
    p.sweeps = sweeps;
    p.sweep = sweep;
    for (int i = 0; i < themepack::N_COLORS; i++) p.color[i] = (uint16_t)(base + i);
    p.color[themepack::R_VOID] = 0x0000;
    p.color[themepack::R_PANEL] = 0x0841;
    p.color[themepack::R_AMBER] = 0xFFE0;   // yellow on black: well over 3:1
    p.color[themepack::R_RED] = 0xF800;     // red on black: about 5:1
    return p;
}

int main() {
    check("three themes", pal::TH_COUNT == 3);
    {   // every name a built-in displays must be refused to a pack
        bool all = true;
        for (int i = 0; i < pal::TH_COUNT; i++) {
            themepack::Pack q; memset(&q, 0, sizeof q);
            strncpy(q.name, pal::themeName((uint8_t)i), themepack::MAX_NAME);
            if (themepack::admit("a", q, nullptr, 0) != themepack::ADMIT_RESERVED) {
                printf("  built-in name not reserved: %s\n", q.name);
                all = false;
            }
        }
        check("admit() reserves every pal::themeName(i) for i < TH_COUNT", all);
    }
    check("cosmic name", !strcmp(pal::themeName(pal::TH_COSMICMUFFIN), "COSMIC MUFFIN"));

    pal::setTheme(pal::TH_COSMICMUFFIN);
    check("neon void applied", pal::VOID == pal::rgb(0x09, 0x07, 0x1C));
    check("neon magenta applied", pal::MAGENTA == pal::rgb(0xFF, 0x1F, 0xCC));
    // Every sweep but the gaze (0x7FD): the muffin has no eyes. The mask must
    // be wide enough to hold bit 10 -- an 8-bit mask would drop the top sweeps.
    check("cosmic offers every sweep but gaze", pal::sweepMask(pal::TH_COSMICMUFFIN) == 0x07FD);

    pal::setTheme(99);   // out of range, from a corrupt/stale NVS value
    check("bad theme id falls back to UMBRAVOX", pal::VOID == pal::rgb(0x07, 0x04, 0x0E));

    printf("built-in descriptors:\n");
    {
        check("header UMBRAVOX", !strcmp(pal::header(pal::TH_UMBRAVOX), "UMBRAVOX"));
        check("header BAPH0MAU5", !strcmp(pal::header(pal::TH_BAPH0MAU5), "CVLT OF BAPH0MAU5"));
        check("header COSMIC", !strcmp(pal::header(pal::TH_COSMICMUFFIN), "COSMIC MVFFIN"));
        check("centrepiece UMBRAVOX heptagram", pal::centrepiece(pal::TH_UMBRAVOX) == pal::CP_HEPTAGRAM);
        check("centrepiece BAPH0MAU5 seal", pal::centrepiece(pal::TH_BAPH0MAU5) == pal::CP_SEAL);
        check("centrepiece COSMIC emblem", pal::centrepiece(pal::TH_COSMICMUFFIN) == pal::CP_EMBLEM);
        check("no emblem for UMBRAVOX", pal::emblem(pal::TH_UMBRAVOX) == nullptr);
        check("no emblem for BAPH0MAU5", pal::emblem(pal::TH_BAPH0MAU5) == nullptr);
        const themepack::Emblem* m = pal::emblem(pal::TH_COSMICMUFFIN);
        check("COSMIC emblem is the muffin", m == &ui::muffinEmblem());
        const themepack::Emblem& e = ui::muffinEmblem();
        check("muffin emblem 72x72x16", e.w == 72 && e.h == 72 && e.ncol == 16);
        check("muffin emblem pixels are the baked ones", e.px == ui::MUFFIN_PX);
        bool palOk = true;
        for (int i = 0; i < ui::MUFFIN_NCOL; i++) if (e.pal[i] != ui::MUFFIN_PAL[i]) palOk = false;
        check("muffin emblem palette is the baked one", palOk);
        for (uint8_t id = 0; id < pal::TH_COUNT; id++)
            check("built-in starts on RADAR", pal::startSweep(id) == ui::SW_RADAR);
        check("no packs yet", pal::themeCount() == pal::TH_COUNT);
        check("findPack on empty table", pal::findPack("X") == -1);
    }

    // UMBRAVOX's geometry and status colours, to compare packs against.
    pal::setTheme(pal::TH_UMBRAVOX);
    const int uDialH = ui::DIAL_H, uSpecH = ui::SPEC_H, uRStar = ui::R_STAR;
    const uint16_t uAmber = pal::AMBER, uRed = pal::RED;
    pal::setTheme(pal::TH_COSMICMUFFIN);
    const int cDialH = ui::DIAL_H, cSpecH = ui::SPEC_H, cRStar = ui::R_STAR;

    printf("packs:\n");
    {
        // slot 0: compact, every sweep but the gaze, starts on star.
        themepack::Pack a = mkPack("SANE", themepack::DIAL_COMPACT, 0x07FD, ui::SW_STAR, 0x1000);
        // slot 1: large, emblem centre, status colours that vanish on its void.
        themepack::Pack b = mkPack("LOUD", themepack::DIAL_LARGE, 0x0014, ui::SW_PULSE, 0x2000);
        b.centre = themepack::CENTRE_EMBLEM;
        b.rite = themepack::RITE_VORTEX;
        b.color[themepack::R_AMBER] = 0x0000;
        b.color[themepack::R_RED] = 0x0841;
        static uint8_t px[4] = {0, 1, 1, 0};
        static themepack::Emblem em;
        em.w = 2; em.h = 2; em.ncol = 2; em.pal[0] = 0x1234; em.pal[1] = 0x4321; em.px = px;

        check("add SANE -> 3", pal::addPack(a, nullptr) == 3);
        check("count 4", pal::themeCount() == 4);
        check("add LOUD -> 4", pal::addPack(b, &em) == 4);
        check("count 5", pal::themeCount() == 5);
        // The caller's copy may go away: the table must hold its own.
        memset(&a, 0x55, sizeof a);
        themepack::Pack x = mkPack("X", themepack::DIAL_COMPACT, 0x0001, ui::SW_RADAR, 0x3000);
        check("add X -> 5", pal::addPack(x, nullptr) == 5);
        check("count 6", pal::themeCount() == 6);
        char nm[8];
        int expect = 6;
        for (int i = 3; i < themepack::MAX_PACKS; i++) {
            snprintf(nm, sizeof nm, "P%d", i);
            const int id = pal::addPack(mkPack(nm, themepack::DIAL_LARGE, 0x0001, 0, 0x4000), nullptr);
            check("next pack id is sequential", id == expect);
            expect++;
            check("count tracks", pal::themeCount() == expect);
        }
        check("eight packs -> eleven themes", pal::themeCount() == 11);
        check("ninth pack refused", pal::addPack(mkPack("NINE", themepack::DIAL_LARGE, 1, 0, 0), nullptr) == -1);
        check("count unchanged after refusal", pal::themeCount() == 11);

        check("findPack SANE", pal::findPack("SANE") == 3);
        check("findPack LOUD", pal::findPack("LOUD") == 4);
        check("findPack X", pal::findPack("X") == 5);
        check("findPack P7", pal::findPack("P7") == 10);
        check("findPack is case-sensitive", pal::findPack("sane") == -1);
        check("findPack: no prefix match", pal::findPack("SAN") == -1);
        check("findPack: no extension match", pal::findPack("SANEX") == -1);
        check("findPack refused pack absent", pal::findPack("NINE") == -1);
        check("findPack empty", pal::findPack("") == -1);
        check("findPack null", pal::findPack(nullptr) == -1);
        check("findPack does not match built-ins", pal::findPack("UMBRAVOX") == -1);

        check("pack name kept after caller clobbers", !strcmp(pal::themeName(3), "SANE"));
        check("pack header", !strcmp(pal::header(3), "HDR SANE"));
        check("pack header LOUD", !strcmp(pal::header(4), "HDR LOUD"));
        check("sweepMask SANE", pal::sweepMask(3) == 0x07FD);
        check("sweepMask LOUD", pal::sweepMask(4) == 0x0014);
        check("startSweep SANE", pal::startSweep(3) == ui::SW_STAR);
        check("startSweep LOUD", pal::startSweep(4) == ui::SW_PULSE);
        check("packRite SANE", pal::packRite(3) == themepack::RITE_COMET);
        check("packRite LOUD", pal::packRite(4) == themepack::RITE_VORTEX);
        check("centrepiece heptagram pack", pal::centrepiece(3) == pal::CP_HEPTAGRAM);
        check("centrepiece emblem pack", pal::centrepiece(4) == pal::CP_EMBLEM);
        check("emblem stored by pointer", pal::emblem(4) == &em);
        check("no emblem given -> null", pal::emblem(3) == nullptr);
        check("out-of-range emblem -> null", pal::emblem(200) == nullptr);

        pal::setTheme(3);
        check("SANE void", pal::VOID == 0x0000);
        check("SANE witch", pal::WITCH == 0x1000 + themepack::R_WITCH);
        check("SANE bone", pal::BONE == 0x1000 + themepack::R_BONE);
        check("SANE green", pal::GREEN == 0x1000 + themepack::R_GREEN);
        check("SANE live hi", pal::LIVE_HI == 0x1000 + themepack::R_LIVEHI);
        check("SANE amber kept", pal::AMBER == 0xFFE0);
        check("SANE red kept", pal::RED == 0xF800);
        check("SANE ramp void,edge,dim,bright",
              pal::RAMP[0] == 0x0000 && pal::RAMP[1] == 0x1000 + themepack::R_EDGE &&
              pal::RAMP[2] == 0x1000 + themepack::R_DIM && pal::RAMP[3] == 0x1000 + themepack::R_BRIGHT);
        check("compact pack has UMBRAVOX geometry",
              ui::DIAL_H == uDialH && ui::SPEC_H == uSpecH && ui::R_STAR == uRStar);

        pal::setTheme(4);
        check("LOUD witch", pal::WITCH == 0x2000 + themepack::R_WITCH);
        check("LOUD amber sanitised to UMBRAVOX's", pal::AMBER == uAmber);
        check("LOUD red sanitised to UMBRAVOX's", pal::RED == uRed);
        check("large pack has COSMIC geometry",
              ui::DIAL_H == cDialH && ui::SPEC_H == cSpecH && ui::R_STAR == cRStar);

        pal::setTheme(11);   // one past the last pack
        check("id past the packs falls back to UMBRAVOX", pal::VOID == pal::rgb(0x07, 0x04, 0x0E));
        check("themeName past the packs is UMBRAVOX", !strcmp(pal::themeName(11), "UMBRAVOX"));
        check("built-ins untouched by packs", pal::sweepMask(pal::TH_COSMICMUFFIN) == 0x07FD);
    }

    printf("saved by name:\n");
    {
        check("name present -> its id", pal::resolveSaved("X", 4) == 5);
        check("name absent -> UMBRAVOX", pal::resolveSaved("GONE", 4) == pal::TH_UMBRAVOX);
        check("empty name, pack id -> UMBRAVOX", pal::resolveSaved("", 4) == pal::TH_UMBRAVOX);
        check("empty name, built-in id kept", pal::resolveSaved("", 2) == 2);
        check("empty name, BAPH0MAU5 kept", pal::resolveSaved("", 1) == 1);
        check("null name, built-in id kept", pal::resolveSaved(nullptr, 2) == 2);
        check("empty name, garbage id -> UMBRAVOX", pal::resolveSaved("", 200) == pal::TH_UMBRAVOX);
    }

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
