#!/usr/bin/env python3
"""Emit exact legacy ring pixels for the two native engine profiles.

Run from any directory; output is ui/radar_geometry.h. Pixel parity is also
checked against the original C++ trigonometric formula by the host tests.
"""
import math
from pathlib import Path
out = ['// Integer radar geometry; regenerate with tools/gen_radar_geometry.py.',
       '#pragma once', '#include <cstdint>', 'namespace radargeometry {',
       'struct Point { int8_t x, y; };']
for r in (9, 18, 23, 27, 28, 46, 56, 69, 85):
    pts = sorted({(int(r * math.cos(a * 3.14159265 / 720.0) + .5),
                   int(r * math.sin(a * 3.14159265 / 720.0) + .5))
                  for a in range(1440)}, key=lambda p: (p[1], p[0]))
    out += [f'inline constexpr Point ring{r}[] = {{']
    for start in range(0, len(pts), 12):
        out += ['    ' + ','.join('{%d,%d}' % p for p in pts[start:start+12]) + ',']
    out += ['};']
for r in (19, 49, 61):
    out += [f'inline constexpr Point star{r}[] = {{' + ','.join(
        '{%d,%d}' % (int(r*math.cos(i*2*3.14159265/7)), int(r*math.sin(i*2*3.14159265/7)))
        for i in range(7)) + '};']
out += ['inline const Point* ring(int r, int& count) {', '    switch (r) {']
for r in (9, 18, 23, 27, 28, 46, 56, 69, 85):
    out += [f'    case {r}: count = sizeof ring{r}/sizeof(Point); return ring{r};']
out += ['    default: count=0; return nullptr;', '    }', '}', '} // namespace radargeometry', '']
(Path(__file__).resolve().parents[1] / 'ui/radar_geometry.h').write_text('\n'.join(out))
