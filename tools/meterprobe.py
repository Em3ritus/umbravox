#!/usr/bin/env python3
"""Measure frame cost on a board for the designer's speed meter (plan 5c): pixels drawn, pixels pushed and time.

    node tools/meter-packs.mjs PACKS                       # the calibration packs
    ~/.platformio/penv/bin/python tools/meterprobe.py --port /dev/ttyUSB0 --board cyd-engine --packs PACKS --out cyd.csv
    ~/.platformio/penv/bin/python tools/meterprobe.py --port /dev/ttyACM0 --board waveshare  --packs PACKS --out ws.csv

Needs an ENGINE build on the board (ws-c5-35-engine, nm-cyd-c5-engine): FRAMELOG is not in the legacy CYD build.
The built-in theme (THEME 0) is measured first, with no pack. Then, for each pack folder under PACKS, the probe:
installs it (tools/sdput.py's protocol; a folder already on the card is overwritten with the probe's copy and
left there afterwards), reboots, selects the theme by name, turns FRAMELOG on, drops 2 s of
settling, records the scan screen for --seconds, sends TEST and records the alert steps (HIGH, MED, LOW, ...)
for 16 s, turns FRAMELOG off, and deletes the pack again. Rows go to --out as CSV:

    board,pack,mode,drawn,pushed,us,gap        mode = scan | low | med | high

tools/meterfit.py turns one or more of these files into web/designer/js/meter-fit.json. Close any serial monitor,
the designer's USB page and other tools first: the port must be free. Exit 0 when every pack was measured.
"""
import argparse
import csv
import importlib.util
import json
import os
import re
import sys
import time
from pathlib import Path

_FRM = re.compile(r"^\[frm\] d (\d+) p (\d+) us (\d+) gap (\d+) a (-?\d+)$")
_THEME = re.compile(r"^theme (\d+) requested \((.*)\)$")
_RANGE = re.compile(r"^THEME 0\.\.(\d+)$")
MODES = {-1: "scan", 0: "low", 1: "med", 2: "high"}


def parse_frm(text):
    """The complete [frm] lines in a console transcript (a last line with no newline may be cut off: dropped)."""
    lines = text.split("\n")
    if not text.endswith("\n"):
        lines = lines[:-1]
    rows = []
    for line in lines:
        m = _FRM.match(line.rstrip("\r"))
        if m:
            d, p, us, gap, a = (int(x) for x in m.groups())
            rows.append({"drawn": d, "pushed": p, "us": us, "gap": gap, "alert": a})
    return rows


def mode_of(alert):
    return MODES.get(alert, "high" if alert > 2 else "scan")


def csv_rows(board, pack, rows):
    return [[board, pack, mode_of(r["alert"]), r["drawn"], r["pushed"], r["us"], r["gap"]] for r in rows]


def theme_reply(line):
    m = _THEME.match(line)
    return (int(m.group(1)), m.group(2)) if m else None


def theme_range(line):
    m = _RANGE.match(line)
    return int(m.group(1)) if m else None


def sdls_dirs(lines):
    """Folder names in an SDLS listing ("     <dir>  name/")."""
    out = set()
    for line in lines:
        if "<dir>" in line:
            name = line.split("<dir>", 1)[1].strip().rstrip("/")
            if name:
                out.add(name)
    return out


def themes_on_card(link):
    link.send("SDLS /themes")
    body, inside = [], False
    for line in link.lines(5.0):
        if line.startswith("=== SDLS END"):
            break
        if line.startswith("=== SDLS"):
            inside = True
        elif inside:
            body.append(line)
    return sdls_dirs(body)


def _sdput():
    spec = importlib.util.spec_from_file_location("sdput", Path(__file__).resolve().parent / "sdput.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def collect(link, seconds):
    text = []
    for line in link.lines(seconds):
        text.append(line + "\n")
    return parse_frm("".join(text))


def select_theme(link, name):
    """THEME by name: ask for the range, then walk down from the last id (packs follow the built-ins). Just after a
    boot the board answers late (it is still loading pack media), so each request waits for the reply carrying ITS
    id, and any reply naming the theme is taken at the id it gives, never at the id last sent."""
    link.send("THEME 9999")
    top = None
    for line in link.lines(6.0):
        top = theme_range(line)
        if top is not None:
            break
    if top is None:
        return None
    for k in range(top, -1, -1):
        link.send("THEME %d" % k)
        for line in link.lines(6.0):
            r = theme_reply(line)
            if r and r[1] == name:
                link.send("THEME %d" % r[0])       # the walk may have moved on: select it again
                for _ in link.lines(1.0):
                    pass
                return r[0]
            if r and r[0] == k:
                break
    return None


def reopen(sd, port, old):
    old.close()
    time.sleep(10.0)                 # the reboot, and the boot banner
    link = sd.Link(port)
    if not link.wait_ready(timeout=90.0):
        raise SystemExit("the board did not come back after the reboot")
    return link


def measure(link, board, pack, seconds):
    link.send("FRAMELOG 1")
    if link.expect("framelog: on (one [frm] line per scan frame; the screen stays lit)", 3.0) is None:
        raise SystemExit("FRAMELOG did not turn on: is this an engine build?")
    for _ in link.lines(2.0):        # settle: the theme switch's full repaint
        pass
    rows = collect(link, seconds)
    link.send("TEST")
    rows += [r for r in collect(link, 16.0) if r["alert"] >= 0]
    link.send("FRAMELOG 0")
    for _ in link.lines(1.0):
        pass
    print("%s: %d scan frames, %d alert frames" % (pack, sum(1 for r in rows if r["alert"] < 0), sum(1 for r in rows if r["alert"] >= 0)))
    return csv_rows(board, pack, rows)


def save(path, rows):
    """Written after every pack, so a failure later keeps what was measured."""
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["board", "pack", "mode", "drawn", "pushed", "us", "gap"])
        w.writerows(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", required=True)
    ap.add_argument("--board", required=True, choices=["waveshare", "cyd-engine"])
    ap.add_argument("--packs", required=True, help="folder written by tools/meter-packs.mjs")
    ap.add_argument("--out", required=True)
    ap.add_argument("--seconds", type=float, default=20.0, help="seconds of scan frames per pack")
    a = ap.parse_args()

    sd = _sdput()
    folders = sorted(p for p in Path(a.packs).iterdir() if (p / "theme.json").is_file())
    link = sd.Link(a.port)
    if not link.wait_ready():
        sys.exit("the board did not answer on %s" % a.port)
    out = []
    before = themes_on_card(link)     # folders already on the card are the owner's: measured, never deleted
    try:
        link.send("THEME 0")
        out += measure(link, a.board, "builtin", a.seconds)
        save(a.out, out)
        for f in folders:
            name = json.loads((f / "theme.json").read_text())["name"]
            folder, files = sd.pack_files(str(f))
            if not sd.put_all(link, files):
                sys.exit("could not install %s" % f.name)
            link.send("REBOOT")
            link = reopen(sd, a.port, link)
            k = select_theme(link, name)
            if k is None:
                sys.exit("%s did not load on the board (theme %r not offered)" % (f.name, name))
            out += measure(link, a.board, f.name, a.seconds)
            save(a.out, out)
            if folder in before:
                print("%s was on the card before the probe: left in place" % folder)
                continue
            for path, _ in files:
                link.send("SDDEL " + path)
                link.expect("OK", 5.0)
            link.send("SDRMDIR " + folder)
            link.expect("OK", 5.0)
        link.send("REBOOT")              # leave the board without the probe's packs loaded
    finally:
        link.close()
    save(a.out, out)
    print("wrote %d frames to %s" % (len(out), os.path.abspath(a.out)))


if __name__ == "__main__":
    main()
