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

inline uint8_t clampMode(uint8_t m) { return m < ui::IDLE_MODE_N ? m : (uint8_t)ui::IDLE_DIM; }
inline uint8_t nextMode(uint8_t m)  { return ui::idlemodeNext(m); }

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
    mode = clampMode(mode);
    p.pwm     = mode == ui::IDLE_OFF ? (uint8_t)0 : dimPwm;
    p.suspend = pwmNow == p.pwm;
    p.anim    = p.suspend && mode == ui::IDLE_ANIM;
    return p;
}

// The touch that finds the panel dimmed/dark only wakes it.
inline bool swallowTouch(bool dimmed) { return dimmed; }

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

// Draw one frame into the canvas and return the only region that changed.
inline Rect drawFrame(ui::Canvas& c, uint32_t frame, uint16_t bg, uint16_t fg) {
    const Pose p = pose(frame);
    const Rect r{(int16_t)(p.cx - BOX_HALF), (int16_t)(p.cy - BOX_HALF),
                 (int16_t)(2 * BOX_HALF + 1), (int16_t)(2 * BOX_HALF + 1)};
    c.fill(r.x, r.y, r.w, r.h, bg);
    if (p.r) c.disc(p.cx, p.cy, p.r, fg);
    return r;
}

} // namespace idlemode
