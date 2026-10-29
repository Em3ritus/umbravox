#!/usr/bin/env python3
"""Hardware check for a freshly flashed CYD: boot probe + scan-screen frame timing.

Why this exists: the C5's early-boot PSRAM init can panic depending on the exact
firmware binary (see "Known issue: early-boot PSRAM panic" in docs/design-notes.md), and every build must be gated on a
run of clean resets.  Frame timing catches draw-time regressions that host tests
cannot see.  Reads the serial console only; sends nothing to the board.

    ~/.platformio/penv/bin/python tools/bootprobe.py                 # 12 resets + 20 s of frames
    ~/.platformio/penv/bin/python tools/bootprobe.py --resets 3 --frames 0
    ~/.platformio/penv/bin/python tools/bootprobe.py --port /dev/ttyUSB1

Needs pyserial (PlatformIO's python has it).  Close any serial monitor, the
designer's USB page and tools/sdput.py first: the port must be free.
Exit status 0 = every reset booted clean, 1 = a panic or no boot was seen.
"""
import argparse
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial missing: run with ~/.platformio/penv/bin/python")


def open_port(path):
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = path, 115200, 0.3
    s.dtr = False                      # keep DTR/RTS low; we pulse RTS ourselves
    s.rts = False
    s.open()
    return s


def read_for(s, seconds):
    end = time.time() + seconds
    buf = b""
    while time.time() < end:
        buf += s.read(8192)
    return buf.decode("utf8", "replace")


def reset(s):
    s.rts = True
    time.sleep(0.1)
    s.rts = False


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--resets", type=int, default=12)
    ap.add_argument("--frames", type=int, default=20, help="seconds of frame timing after the resets (0 = skip)")
    ap.add_argument("--boot-wait", type=float, default=5.0, help="seconds to listen after each reset")
    a = ap.parse_args()

    try:
        s = open_port(a.port)
    except Exception as e:             # noqa: BLE001
        sys.exit(f"cannot open {a.port}: {e}")

    bad = 0
    heaps = []
    for i in range(a.resets):
        reset(s)
        t = read_for(s, a.boot_wait)
        panic = "Guru Meditation" in t
        booted = "UMBRAVOX bring-up" in t
        m = re.search(r"reset=\w+ heap=(\d+)KB", t)
        if m:
            heaps.append(int(m.group(1)))
        if panic or not booted:
            bad += 1
        print(f"reset {i + 1:2d}: {'PANIC' if panic else 'clean'}{'' if booted else ' NO BOOT'}")
    if a.resets:
        print(f"{a.resets - bad}/{a.resets} clean boots" + (f"; boot heap {min(heaps)}-{max(heaps)} KB" if heaps else ""))

    if a.frames:
        t = read_for(s, a.frames)
        fr = re.findall(r"\[scan \] draw\s+(\d+) blit\s+(\d+) ms", t)
        theme = re.search(r"theme (\d+) \(([^)]*)\)", t)
        if fr:
            d = sorted(int(x) for x, _ in fr)
            b = sorted(int(y) for _, y in fr)
            print(f"scan frames {len(d)}: draw min/median/max {d[0]}/{d[len(d) // 2]}/{d[-1]} ms, "
                  f"blit {b[0]}/{b[len(b) // 2]}/{b[-1]} ms" + (f"  (theme {theme.group(2)})" if theme else ""))
            print("reference: about 12 ms median on the NM-CYD-C5 scan screen (landscape, pack with an emblem); investigate a rise past ~14 ms")
        else:
            print("no scan-screen frame lines seen (screen dark or not on the scan screen)")
    s.close()
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
