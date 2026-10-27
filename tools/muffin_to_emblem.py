#!/usr/bin/env python3
"""Convert ui/muffin.h into an emblem.bin (the theme-pack picture format).

Run from the repo root:
    python3 -I tools/muffin_to_emblem.py [ui/muffin.h [out/emblem.bin]]
Default output: sim/packs/cosmic-clone/emblem.bin

Format (little-endian): 'U','V','E',1, w, h, ncol, 0, ncol RGB565 values,
then w*h palette indices. Deterministic; standard library only.
"""
import re
import sys


def die(msg):
    sys.stderr.write("muffin_to_emblem: %s\n" % msg)
    sys.exit(1)


def const_int(text, name):
    m = re.search(r"constexpr\s+int\s+%s\s*=\s*(\d+)\s*;" % name, text)
    if not m:
        die("cannot find constexpr int %s" % name)
    return int(m.group(1))


def table(text, name, base):
    m = re.search(r"%s\s*\[[^\]]*\]\s*=\s*\{([^}]*)\}" % name, text)
    if not m:
        die("cannot find table %s" % name)
    body = re.sub(r"//[^\n]*", "", m.group(1))
    vals = [t for t in re.split(r"[\s,]+", body) if t]
    try:
        return [int(t, base) for t in vals]
    except ValueError:
        die("table %s has a non-numeric entry" % name)


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "ui/muffin.h"
    dst = sys.argv[2] if len(sys.argv) > 2 else "sim/packs/cosmic-clone/emblem.bin"
    try:
        with open(src, "r", encoding="utf-8") as f:
            text = f.read()
    except OSError as e:
        die("cannot read %s (run from the repo root): %s" % (src, e))
    w, h, nc = (const_int(text, n) for n in ("MUFFIN_W", "MUFFIN_H", "MUFFIN_NCOL"))
    pal = table(text, "MUFFIN_PAL", 16)
    px = table(text, "MUFFIN_PX", 10)
    if not (1 <= w <= 96 and 1 <= h <= 96 and 2 <= nc <= 16):
        die("size or colour count outside the emblem limits")
    if len(pal) != nc:
        die("MUFFIN_PAL has %d entries, expected %d" % (len(pal), nc))
    if len(px) != w * h:
        die("MUFFIN_PX has %d entries, expected %d" % (len(px), w * h))
    if any(not 0 <= v <= 0xFFFF for v in pal) or any(not 0 <= v < nc for v in px):
        die("a palette value or pixel index is out of range")
    out = bytearray(b"UVE\x01" + bytes([w, h, nc, 0]))
    for v in pal:
        out += v.to_bytes(2, "little")
    out += bytes(px)
    try:
        with open(dst, "wb") as f:
            f.write(out)
    except OSError as e:
        die("cannot write %s: %s" % (dst, e))
    print("%s: %dx%d, %d colours, %d bytes" % (dst, w, h, nc, len(out)))


main()
