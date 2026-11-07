#!/usr/bin/env python3
"""Emit antialiased circle coverage for supported engine profiles.

Run from any directory; output is ui/radar_geometry.h. Coverage is computed offline; firmware only blends existing strip pixels.
"""
import math
from pathlib import Path
out = ['// Integer radar geometry; regenerate with tools/gen_radar_geometry.py.',
       '#pragma once', '#include <cstdint>', 'namespace radargeometry {',
       'struct Point { int8_t x, y; uint8_t coverage=16; };']
for r in (9, 13, 18, 19, 23, 26, 27, 28, 32, 34, 36, 38, 39, 46, 55, 56, 58, 65, 68, 69, 85, 98, 102):
    # Offline 8x8 area coverage of a one-pixel circular stroke. Symmetric
    # sampling avoids the old int(negative + .5) half-circle bias.
    pts = []
    for y in range(-r-1, r+2):
        for x in range(-r-1, r+2):
            if abs(math.hypot(x,y)-r) > 1.25:
                continue
            hits = sum((r-.5)**2 <= (x+(sx+.5)/8-.5)**2 +
                       (y+(sy+.5)/8-.5)**2 <= (r+.5)**2
                       for sy in range(8) for sx in range(8))
            alpha = (hits*16+32)//64
            if alpha:
                pts.append((x,y,alpha))
    out += [f'inline constexpr Point ring{r}[] = {{']
    for start in range(0, len(pts), 12):
        out += ['    ' + ','.join('{%d,%d,%d}' % p for p in pts[start:start+12]) + ',']
    out += ['};']
for r in (19, 49, 61, 73):
    out += [f'inline constexpr Point star{r}[] = {{' + ','.join(
        '{%d,%d}' % (int(r*math.cos(i*2*3.14159265/7)), int(r*math.sin(i*2*3.14159265/7)))
        for i in range(7)) + '};']
out += ['inline const Point* ring(int r, int& count) {', '    switch (r) {']
for r in (9, 13, 18, 19, 23, 26, 27, 28, 32, 34, 36, 38, 39, 46, 55, 56, 58, 65, 68, 69, 85, 98, 102):
    out += [f'    case {r}: count = sizeof ring{r}/sizeof(Point); return ring{r};']
out += ['    default: count=0; return nullptr;', '    }', '}', '} // namespace radargeometry', '']
(Path(__file__).resolve().parents[1] / 'ui/radar_geometry.h').write_text('\n'.join(out))
