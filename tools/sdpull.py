#!/usr/bin/env python3
"""List and pull files from the UMBRAVOX SD card over the wired USB console.

    sdpull.py PORT ls  [dir]              parsed listing: size, name (directories marked)
    sdpull.py PORT get <path> [--out DIR] copy one file (SDCAT) to DIR (default: .)

    ~/.platformio/penv/bin/python tools/sdpull.py /dev/ttyACM0 ls /umbravox/sessions
    ~/.platformio/penv/bin/python tools/sdpull.py /dev/ttyACM0 get /perf.csv --out pulled/

Read-only: it only sends SDLS and SDCAT. Opening the port resets the board (and ends a recording in progress), so stop
recording first. Like sdput.py it holds DTR and RTS low and waits for the board to boot.

SDCAT streams the file through the same console as the device's own log lines, so a pull can contain chatter in the
middle. A .csv/.CSV file is cleaned: only rows with the header's number of fields are kept, and the count of dropped
lines is printed. Any other text file (.log, .txt, .json, .gpx) is written line by line
(line endings normalised). Binary files (.vid, .snd, .bin, .pcap) are refused: the console's line handling would corrupt them.
"""
import csv
import importlib.util
import io
import os
import re
import sys
import time
from pathlib import Path

# SDLS prints "%10lu  name" (older firmware "%8lu  name") or "<dir>  name/". Exactly two spaces separate the size from the
# name, which keeps diag lines ("0000025191 WARN ...", one space) and other console text out of the listing.
_FILE = re.compile(r"^\s*(\d+)  (\S.*)$")
_DIR = re.compile(r"^\s*<dir>  (\S.*)/$")


def parse_listing(text):
    entries = []
    inside = False
    for line in text.splitlines():
        line = line.rstrip("\r")
        if line.startswith("=== SDLS END"):
            break
        if line.startswith("=== SDLS"):
            inside = True
            continue
        if not inside:
            continue
        m = _DIR.match(line)
        if m:
            entries.append({"name": m.group(1), "size": None, "is_dir": True})
            continue
        m = _FILE.match(line)
        if m:
            entries.append({"name": m.group(2), "size": int(m.group(1)), "is_dir": False})
    return entries


def cat_body(text):
    """The lines between '=== SDCAT <path> ===' and '=== SDCAT END ==='; empty if the file was not there."""
    body, inside = [], False
    for line in text.splitlines():
        line = line.rstrip("\r")
        if line.startswith("=== SDCAT END"):
            return body
        if inside:
            body.append(line)
        elif line.startswith("=== SDCAT "):
            inside = True
    return body if inside else []


def csv_rows(lines):
    """(header, rows, dropped). Keeps the header and every line that parses to the header's number of fields."""
    header, rows, dropped = None, [], 0
    for line in lines:
        if not line.strip():
            continue
        try:
            fields = next(csv.reader(io.StringIO(line)))
        except (csv.Error, StopIteration):
            dropped += 1
            continue
        if header is None:
            header = fields
        elif len(fields) == len(header):
            rows.append(fields)
        else:
            dropped += 1
    return header, rows, dropped


_TEXT = (".csv", ".log", ".txt", ".json", ".gpx")


def is_text_file(name):
    """The console delivers lines, so only text files survive a pull byte for byte (apart from line endings)."""
    return name.lower().endswith(_TEXT)


def _link(port):
    here = Path(__file__).resolve().parent
    spec = importlib.util.spec_from_file_location("sdput_link", here / "sdput.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    link = mod.Link(port)
    if not link.wait_ready():
        raise SystemExit("board did not answer SDLS (is it booted, and is this the console port?)")
    return link


def _collect(link, command, end_marker, timeout):
    link.send(command)
    out = []
    for line in link.lines(timeout):
        out.append(line)
        if line.startswith(end_marker):
            break
    return "\n".join(out) + "\n"


def cmd_ls(link, d):
    d = d or "/"
    for e in parse_listing(_collect(link, "SDLS " + d, "=== SDLS END", 30)):
        print("%10s  %s%s" % ("<dir>" if e["is_dir"] else e["size"], e["name"], "/" if e["is_dir"] else ""))


def cmd_get(link, path, out_dir):
    parent, leaf = os.path.split(path)
    listed = {e["name"]: e["size"] for e in parse_listing(_collect(link, "SDLS " + (parent or "/"), "=== SDLS END", 30))}
    size = listed.get(leaf)
    if size is None:
        raise SystemExit("%s is not on the card" % path)
    if not is_text_file(leaf):
        raise SystemExit("%s is not a text file: SDCAT goes through the console's line handling, which would corrupt it. "
                         "Take the card out, or use the designer/sdput for theme packs." % leaf)
    text = _collect(link, "SDCAT " + path, "=== SDCAT END", 60 + size / 8000.0)   # ~11 KB/s measured; allow for slower
    body = cat_body(text)
    os.makedirs(out_dir, exist_ok=True)
    dest = os.path.join(out_dir, leaf)
    if leaf.lower().endswith(".csv"):
        header, rows, dropped = csv_rows(body)
        with open(dest, "w", newline="") as fh:
            w = csv.writer(fh, lineterminator="\r\n")
            if header:
                w.writerow(header)
            w.writerows(rows)
        print("%s: %d rows, %d console/torn lines dropped (card lists %d bytes)" % (dest, len(rows), dropped, size))
    else:
        data = ("\n".join(body) + "\n").encode("utf-8", "replace")
        with open(dest, "wb") as fh:
            fh.write(data)
        print("%s: %d bytes received (line endings normalised), card lists %d" % (dest, len(data), size))


def main(argv):
    if len(argv) < 3 or argv[2] not in ("ls", "get"):
        sys.exit(__doc__)
    out_dir = "."
    if "--out" in argv:
        i = argv.index("--out")
        out_dir = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
    link = _link(argv[1])
    try:
        if argv[2] == "ls":
            cmd_ls(link, argv[3] if len(argv) > 3 else "/")
        else:
            if len(argv) < 4:
                sys.exit(__doc__)
            cmd_get(link, argv[3], out_dir)
    finally:
        link.close()


if __name__ == "__main__":
    main(sys.argv)
