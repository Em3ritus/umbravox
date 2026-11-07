// Host test: packmedia::Controller, which decides when a theme pack's radar clip and alert sounds are read from the SD
// card into PSRAM, and when they are dropped. The device (device/themeload.cpp) does the I/O; this is only the decision.
//   - media is loaded for the theme on screen, after it has stayed selected for SETTLE_MS (scrolling the theme list must
//     not read 1.4 MB per step), or at once at boot (`immediate`);
//   - one pack is resident at a time: switching unloads the old before loading the new;
//   - a load that failed is never retried (no loop on a bad file) until the pack is forgotten;
//   - time is compared with signed differences, so the 32-bit millis() wrap does not matter.
// Build: make -C sim test_packmedia
#include "../ui/packmedia.h"
#include <cstdio>
using packmedia::Controller;
using packmedia::Step;

static int fails = 0;
static void check(const char* what, bool ok) { printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what); if (!ok) fails++; }

using C = Controller<12>;
static bool is(const Step& s, Step::Kind k, int slot) { return s.kind == k && s.slot == slot; }

int main() {
    { C c; c.setHasMedia(3, true);
      c.want(packmedia::NONE, 0);
      check("a built-in on screen and nothing resident: nothing to do", c.next(10000).kind == Step::NOTHING); }

    { C c; c.setHasMedia(3, false);
      c.want(3, 0);
      check("a pack without a clip or sounds never loads", c.next(100000).kind == Step::NOTHING); }

    { C c; c.setHasMedia(3, true);
      c.want(3, 1000);
      check("not before it has settled", c.next(1000 + packmedia::SETTLE_MS - 1).kind == Step::NOTHING);
      check("loads once it has stayed selected for SETTLE_MS", is(c.next(1000 + packmedia::SETTLE_MS), Step::LOAD, 3)); }

    { C c; c.setHasMedia(3, true);
      c.want(3, 1000, true);
      check("immediate (boot): loads at once", is(c.next(1000), Step::LOAD, 3)); }

    { C c; c.setHasMedia(3, true);
      c.want(3, 0, true);
      const Step s = c.next(0);
      c.done(s, true);
      check("after a good load the pack is resident", c.resident() == 3);
      check("and there is nothing more to do", c.next(5000).kind == Step::NOTHING);
      c.want(3, 6000);                               // called every frame with the same slot
      check("asking for the resident pack again does nothing", c.next(9000).kind == Step::NOTHING && c.resident() == 3); }

    { C c; c.setHasMedia(1, true); c.setHasMedia(2, true);
      c.want(1, 0);
      c.want(2, 300);                                 // scrolled on before 1 settled
      check("scrolling past a pack does not load it", c.next(300 + packmedia::SETTLE_MS - 1).kind == Step::NOTHING);
      check("the pack the scroll stopped on loads, not the one passed", is(c.next(300 + packmedia::SETTLE_MS), Step::LOAD, 2)); }

    { C c; c.setHasMedia(1, true); c.setHasMedia(2, true);
      c.want(1, 0, true); c.done(c.next(0), true);
      c.want(2, 1000);
      const Step u = c.next(1000 + packmedia::SETTLE_MS);
      check("switching packs unloads the resident one first", is(u, Step::UNLOAD, 1));
      c.done(u, true);
      check("nothing is resident between the two steps", c.resident() == packmedia::NONE);
      check("then loads the new pack", is(c.next(1000 + packmedia::SETTLE_MS), Step::LOAD, 2)); }

    { C c; c.setHasMedia(1, true);
      c.want(1, 0, true); c.done(c.next(0), true);
      c.want(packmedia::NONE, 1000);                  // back to a built-in theme
      check("leaving for a built-in unloads after the settle time", is(c.next(1000 + packmedia::SETTLE_MS), Step::UNLOAD, 1));
      c.done(Step{Step::UNLOAD, 1}, true);
      check("and then idles", c.next(9000).kind == Step::NOTHING); }

    { C c; c.setHasMedia(1, true);
      c.want(1, 0, true);
      const Step s = c.next(0);
      c.done(s, false);
      check("a failed load leaves nothing resident", c.resident() == packmedia::NONE);
      check("and is never retried", c.next(60000).kind == Step::NOTHING);
      check("the pack is remembered as failed", c.failed(1));
      c.forget(1);
      c.want(1, 70000);
      check("until it is forgotten (the card changed)", is(c.next(70000 + packmedia::SETTLE_MS), Step::LOAD, 1)); }

    { C c; c.setHasMedia(4, true);
      const uint32_t t0 = 0xFFFFFF00u;                // millis() about to wrap
      c.want(4, t0);
      check("settle time is wrap-safe: not early", c.next(t0 + 100u).kind == Step::NOTHING);
      check("settle time is wrap-safe: loads after the wrap", is(c.next(t0 + 0x200u), Step::LOAD, 4)); }

    { C c;
      c.setHasMedia(99, true); c.setHasMedia(-5, true); c.want(99, 0, true); c.want(-5, 0, true);
      check("an out-of-range slot is ignored, not indexed", c.next(0).kind == Step::NOTHING); }

    // --- Progress: a load cut into slices so the screen keeps updating between them -------------------------------
    { packmedia::Progress p; p.start(100, 50);
      check("two files: not done at the start", !p.done());
      auto a = p.next(64);
      check("first slice: file 0, offset 0, the slice size", a.file == 0 && a.off == 0 && a.len == 64);
      p.advance(a.len);
      auto b = p.next(64);
      check("second slice finishes file 0 with what is left", b.file == 0 && b.off == 64 && b.len == 36);
      p.advance(b.len);
      auto c = p.next(64);
      check("then file 1 from offset 0", c.file == 1 && c.off == 0 && c.len == 50);
      p.advance(c.len);
      check("done once every byte of both is read", p.done() && p.next(64).len == 0); }

    { packmedia::Progress p; p.start(0, 30);
      auto a = p.next(64);
      check("a missing first file is skipped", a.file == 1 && a.len == 30); p.advance(30);
      check("and the load ends", p.done()); }

    { packmedia::Progress p; p.start(0, 0);
      check("nothing to read: done at once", p.done()); }

    { packmedia::Progress p; p.start(10, 20);
      p.skipFile();
      auto a = p.next(64);
      check("skipFile abandons the current file and moves to the next", a.file == 1 && a.off == 0 && a.len == 20);
      p.skipFile();
      check("skipping the last file ends the load", p.done()); }

    { packmedia::Progress p; p.start(1000, 0); uint32_t total = 0, n = 0;
      while (!p.done()) { auto s = p.next(256); total += s.len; n++; p.advance(s.len); }
      check("slices add up to the file, none over the slice size", total == 1000 && n == 4); }

    // Only a panel at least WIDE_PANEL_W wide draws a pack's radar clip, so a narrower one never reads it from the card.
    check("the 480 px Waveshare reads the clip", packmedia::clipWanted(480));
    check("the 400 px threshold itself is wide", packmedia::clipWanted(400));
    check("the 320 px CYD and 2.8 inch panels skip the clip", !packmedia::clipWanted(320) && !packmedia::clipWanted(399));
    check("a clip of N bytes is read as N on a wide panel", packmedia::clipBytes(650262, 480) == 650262);
    check("and as 0 on a narrow one", packmedia::clipBytes(650262, 320) == 0);

    printf(fails ? "FAILED %d\n" : "packmedia: decisions passed\n", fails);
    return fails ? 1 : 0;
}
