// Caller-supplied DOMAIN scene fixtures for host rendering/tests. The compositor
// carries no embedded data; this is where the demo values live. Map dots and
// track points are PRE-PROJECTED pixel offsets from the radar centre (+y south),
// exactly as ui::Plot arrives from the device's plotproj.h.
#pragma once
#include "../ui/domain_render.h"

namespace domfix {

// Located sightings, offset from the radar centre. Two carry a signature ring.
static const domr::MapDot DOTS[6] = {
    { -18, -10, 0, false }, {  24,  12, 1, false }, {  -6,  30, 2, false },
    {  40, -22, 0, true  }, { -34,  18, 1, false }, {   8, -40, 2, true  },
};
// A short track behind you (older first; the renderer fades by index).
static const domr::TrackDot TRACK[5] = {
    { -30, 20 }, { -22, 14 }, { -14, 10 }, { -6, 4 }, { 0, 0 },
};
// A full wide page: 2 located rows, strongest first (the policy's output).
static const domr::DomRow ROWS[2] = {
    { "w:aabbccddeeff", "FLOCK-CAM", 0 },
    { "b:112233445566", "TILE-TAG",  2 },
};

// A populated DOMAIN scene: fresh 3D fix, recording off, 6 located within range.
inline domr::DomainScene demo(uint8_t theme, bool wide = true, uint32_t now = 5000) {
    domr::DomainScene s;
    s.gps = { true, 3, 12, 500 };          // valid 3D fix, fresh
    s.nodes = 3; s.hasCell = true; s.battPct = 78; s.rec = domr::Rec::Off;
    s.dots = DOTS; s.nDots = 6;
    s.track = TRACK; s.nTrack = 5;
    s.plotShown = 6; s.plotTotal = 9; s.plotBeyond = 3; s.rangeM = 250;
    if (wide) { s.rows = ROWS; s.nRows = 2; s.pageLo = 1; s.pageHi = 2; s.listTotal = 9; }
    else      { s.rows = nullptr; s.nRows = 0; s.pageLo = 0; s.pageHi = 0; s.listTotal = 9; }
    s.theme = theme; s.now = now;
    return s;
}

}  // namespace domfix
