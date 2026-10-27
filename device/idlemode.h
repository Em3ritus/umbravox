#pragma once
// What the screen does once the idle timeout has fired: DIM, OFF or ANIM.
// Pure decisions, host-tested (sim/test_idlemode.cpp); main.cpp only applies
// them. The mode is Settings.idleMode and matters only while the idle timeout
// is enabled (idleIdx != 0).
//
//   DIM   backlight ramps to bright/8, drawing and blitting suspended (the
//         original behaviour and the default).
//   OFF   backlight ramps to 0; drawing and blitting suspended.
//   ANIM  backlight ramps to the same dim level; the normal UI is suspended
//         and a tiny breathing dot is drawn at <= 2 fps, blitting only its
//         own 11x11 rectangle (~0.6% of the screen per frame).
//
// In all three the touch that wakes the panel is swallowed (a panel you
// cannot see, or can barely see, is a panel you cannot aim at).
#include <stdint.h>
#include "../ui/model.h"
#include "../ui/canvas.h"

namespace idlemode {

struct Rect { int16_t x, y, w, h; };

// Mode clamping and stepping live in ui (clampSettings, idleStep): one
// source of truth. An unknown mode here simply behaves as DIM.

struct Plan {
    uint8_t pwm;       // backlight target
    bool    suspend;   // skip the normal draw + blit this pass
    bool    anim;      // run the low-power animation instead (implies suspend)
};

// dimmed: the idle timer has fired and no touch/alert has woken it since.
// pwmNow: the backlight level actually applied this pass (the ramp).
// The panel only counts as "off" once the ramp has arrived, so the fade
// itself is still drawn, exactly as before.
inline Plan plan(uint8_t mode, bool dimmed, uint8_t pwmNow, uint8_t brightPwm,
                 uint8_t dimPwm, bool idleEnabled = true) {
    Plan p{brightPwm, false, false};
    if (!idleEnabled || !dimmed) return p;
    p.pwm     = mode == ui::IDLE_OFF ? (uint8_t)0 : dimPwm;
    p.suspend = pwmNow == p.pwm;
    p.anim    = p.suspend && mode == ui::IDLE_ANIM;
    return p;
}

// ---- the animation ------------------------------------------------------

constexpr uint32_t ANIM_FRAME_MS = 500;      // 2 fps, the ceiling
constexpr int      MAX_R    = 4;             // largest dot radius
constexpr int      BOX_HALF = MAX_R + 1;     // box is (2*BOX_HALF+1)^2 around the centre

// Frame number from ms since the animation started.
inline uint32_t animFrame(uint32_t msSinceStart) { return msSinceStart / ANIM_FRAME_MS; }

struct Pose { int16_t cx, cy; uint8_t r; };

// A dot that breathes (8 frames = 4 s: 1 2 3 4 3 2 1 and a blank) and, after
// each breath, reappears at the next of four spots. It only moves while
// nothing is drawn, so the old spot is always already clean.
inline Pose pose(uint32_t frame) {
    static const uint8_t R[8] = {1, 2, 3, 4, 3, 2, 1, 0};
    static const int16_t X[4] = {40, 120, 120, 40};
    static const int16_t Y[4] = {36, 36, 84, 84};
    const uint32_t spot = (frame / 8) % 4;
    return Pose{X[spot], Y[spot], R[frame % 8]};
}

constexpr uint32_t NO_FRAME = 0xFFFFFFFFu;

struct Dirty { Rect r[2]; int n; };

inline Rect boxOf(const Pose& p) {
    return Rect{(int16_t)(p.cx - BOX_HALF), (int16_t)(p.cy - BOX_HALF),
                (int16_t)(2 * BOX_HALF + 1), (int16_t)(2 * BOX_HALF + 1)};
}

// Draw one frame into the canvas and return the regions that changed. `prev`
// is the last frame actually drawn (NO_FRAME if none): if the loop stalled and
// frames were skipped, its box is erased too, so no residue is left behind.
inline Dirty drawFrame(ui::Canvas& c, uint32_t frame, uint32_t prev, uint16_t bg, uint16_t fg) {
    Dirty d{{}, 0};
    const Pose p = pose(frame);
    if (prev != NO_FRAME) {
        const Pose q = pose(prev);
        if (q.cx != p.cx || q.cy != p.cy) {
            const Rect o = boxOf(q);
            c.fill(o.x, o.y, o.w, o.h, bg);
            d.r[d.n++] = o;
        }
    }
    const Rect r = boxOf(p);
    c.fill(r.x, r.y, r.w, r.h, bg);
    if (p.r) c.disc(p.cx, p.cy, p.r, fg);
    d.r[d.n++] = r;
    return d;
}

// Enter/exit state machine, run every loop pass (not only while dark).
// `animOn` is plan().anim. enter: blank the canvas and blit it whole once.
// exit: the animation overwrote the canvas, so the next UI frame must be a
// full repaint (wake, mode changed away from ANIM, timeout set to NEVER...).
struct AnimState { bool active = false; uint32_t start = 0; uint32_t last = NO_FRAME; };
struct Step { bool enter, exit, draw; uint32_t frame, prev; };   // prev: last frame drawn before this one

inline Step animStep(AnimState& s, bool animOn, uint32_t nowMs) {
    Step st{false, false, false, 0, NO_FRAME};
    if (!animOn) {
        if (s.active) { s.active = false; st.exit = true; }
        return st;
    }
    if (!s.active) { s.active = true; s.start = nowMs; s.last = NO_FRAME; st.enter = true; }
    st.frame = animFrame(nowMs - s.start);
    st.prev = s.last;
    if (st.frame != s.last) { st.draw = true; s.last = st.frame; }
    return st;
}

} // namespace idlemode
