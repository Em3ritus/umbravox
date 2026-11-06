// Which theme pack's radar clip and alert sounds are in memory, and when to swap them. Pure decisions (host-tested in
// sim/test_packmedia.cpp); device/themeload.cpp does the SD reads and the guard bracket around them.
//
// A pack's radar.vid / alert.snd are not read at boot: they are read from the card when the pack's theme is selected, into
// one fixed buffer each, and dropped when another theme is selected. So the card can hold more packs than PSRAM can hold
// clips. Only the theme on screen has its media; everything else asks pal::clip()/pal::sound() and gets null, which the
// renderers already treat as "no video" and "built-in beep".
//
// Use: every draw, want(theme's slot, now); then `Step s = next(now)`; do what it says; done(s, ok). At boot pass
// immediate=true so the saved theme's media is there before the first frame.
#pragma once
#include <stdint.h>

namespace packmedia {

constexpr int NONE = -1;                    // "no pack": a built-in theme, or nothing resident
// A theme must stay selected this long before its media is read. Stepping through the theme list passes a theme every
// few hundred ms at most, and a pack's media is up to 1.4 MB: only the theme the list stops on is loaded.
constexpr uint32_t SETTLE_MS = 400;

struct Step {
    enum Kind : uint8_t { NOTHING, UNLOAD, LOAD };
    Kind kind = NOTHING;
    int  slot = NONE;
};

template <int N>
class Controller {
public:
    // The scan found a clip and/or sounds in this pack's folder (by size; they are checked when read).
    void setHasMedia(int slot, bool has) { if (inRange(slot)) has_[slot] = has; }

    // The pack slot of the theme on screen now (NONE for a built-in). Cheap: call it every frame.
    void want(int slot, uint32_t nowMs, bool immediate = false) {
        if (slot != NONE && !inRange(slot)) slot = NONE;      // a slot from NVS or a bug is never an index
        if (slot != want_) { want_ = slot; since_ = nowMs; immediate_ = immediate; }
        else if (immediate) immediate_ = true;
    }

    // The next thing to do, or NOTHING. After done() ask again: a switch is UNLOAD, then LOAD.
    Step next(uint32_t nowMs) const {
        const int target = (want_ != NONE && has_[want_] && !failed_[want_]) ? want_ : NONE;
        if (target == resident_) return Step{};
        const bool settled = immediate_ || (int32_t)(nowMs - since_) >= (int32_t)SETTLE_MS;   // signed: millis() wraps
        if (!settled) return Step{};
        if (resident_ != NONE) return Step{Step::UNLOAD, resident_};
        return Step{Step::LOAD, target};
    }

    // The device did `s`. A failed LOAD marks the pack failed: it is not asked for again (no retry loop on a bad file).
    void done(const Step& s, bool ok) {
        if (s.kind == Step::UNLOAD) resident_ = NONE;
        else if (s.kind == Step::LOAD) {
            if (ok) resident_ = s.slot;
            else { resident_ = NONE; if (inRange(s.slot)) failed_[s.slot] = true; }
        }
        immediate_ = false;
    }

    int  resident() const { return resident_; }
    bool failed(int slot) const { return inRange(slot) && failed_[slot]; }
    void forget(int slot) { if (inRange(slot)) failed_[slot] = false; }        // the card changed: allow another try

private:
    static bool inRange(int slot) { return slot >= 0 && slot < N; }
    bool     has_[N] = {};
    bool     failed_[N] = {};
    int      resident_ = NONE;
    int      want_ = NONE;
    uint32_t since_ = 0;
    bool     immediate_ = false;
};

} // namespace packmedia
