#!/usr/bin/env python3
"""Install theme packs and delete files on the UMBRAVOX SD card over the wired
USB console, without pulling the card.

    sdput.py PORT put   <folder>    upload <folder>/emblem.bin (if present), then theme.json
    sdput.py PORT del   <path>      delete one file (theme files and old WD-*.csv logs only)
    sdput.py PORT rmdir <folder>    remove an EMPTY /themes/<folder>
    sdput.py PORT ls    [dir]       list the card's root (or a directory)
    sdput.py PORT reboot            restart the board (packs load at boot)
    sdput.py selftest               check the CRC-32, hex and folder-name rules offline
    sdput.py transcript <folder>    print the exact lines `put` sends (sim/ golden test)

Wired console only: this talks to the board's USB UART (CH340, 115200 8N1)
and nothing else. The device protocol and the file policy are in
device/sdcmd.h. Run it with PlatformIO's python, which has pyserial:
    ~/.platformio/penv/bin/python tools/sdput.py /dev/ttyUSB0 put my-pack/

The port is opened with DTR and RTS held low (dtr=False, rts=False set before
open()), because pyserial's default raises them and on the CYD's CH340 the
auto-reset circuit can turn that into a board reset. Linux still drives both
lines for an instant inside open() itself; if your board resets when the port
opens, run `stty -F PORT -hupcl` once first.

Exit status: 0 when every step was confirmed by the device, 1 otherwise.
"""
import os
import re
import sys
import time
import zlib

CHUNK = 96               # raw bytes per D line (device: sdcmd::CHUNK_MAX)
LINE_CAP = 200           # device: sdcmd::LINE_CAP
MAX_JSON = 4096          # ui/themepack.h
MAX_EMBLEM = 9728
RETRIES = 3              # a failed chunk is resent up to this many times
DOS_NAMES = {"con", "prn", "aux", "nul"} | {p + str(d) for p in ("com", "lpt") for d in range(10)}


# --- the pure parts (shared with the C++ test through the golden transcript) ---
def crc32(data):
    """zlib / IEEE 802.3 CRC-32, as device/sdcmd.h crc32()."""
    return zlib.crc32(data) & 0xFFFFFFFF


def folder_name(name):
    """web/designer/js/pack.js folderName(): lower case, runs of anything but
    a-z0-9 become '-', trimmed, at most 31 characters, 'pack' if empty."""
    f = re.sub(r"[^a-z0-9]+", "-", str(name or "").lower())
    f = f.strip("-")[:31].rstrip("-")
    return f or "pack"


def folder_ok(f):
    """themepack::folderNameOk plus sdcmd's DOS-device-name rule."""
    return bool(re.fullmatch(r"[A-Za-z0-9_-]{1,31}", f)) and f.lower() not in DOS_NAMES


def chunk_line(offset, data):
    assert 0 < len(data) <= CHUNK
    return "D %d %s" % (offset, data.hex())


def pack_files(folder):
    """[(device path, bytes)] for a pack folder: emblem.bin first, so a
    theme.json that needs it never lands on the card without it."""
    folder = os.path.normpath(folder)
    name = folder_name(os.path.basename(folder))
    if not folder_ok(name):
        raise ValueError("folder name %r is not usable on the device" % name)
    files = []
    emb = os.path.join(folder, "emblem.bin")
    js = os.path.join(folder, "theme.json")
    if not os.path.isfile(js):
        raise ValueError("%s has no theme.json" % folder)
    for path, cap, dev in ((emb, MAX_EMBLEM, "emblem.bin"), (js, MAX_JSON, "theme.json")):
        if not os.path.isfile(path):
            continue
        with open(path, "rb") as fh:
            data = fh.read(cap + 1)
        if not data:
            raise ValueError("%s is empty" % path)
        if len(data) > cap:
            raise ValueError("%s is over %d bytes" % (path, cap))
        files.append(("/themes/%s/%s" % (name, dev), data))
    return name, files


def put_steps(path, data):
    """(line to send, the reply that confirms it) for one file."""
    crc = crc32(data)
    steps = [("SDPUT %s %d %08x" % (path, len(data), crc), "OK put %s %d" % (path, len(data)))]
    for off in range(0, len(data), CHUNK):
        steps.append((chunk_line(off, data[off:off + CHUNK]), "OK %d" % off))
    steps.append(("END", "DONE %d %08x" % (len(data), crc)))
    for line, _ in steps:
        assert len(line) <= LINE_CAP
    return steps


def transcript(folder):
    name, files = pack_files(folder)
    out = ["# tools/sdput.py transcript of the pack folder '%s'" % name,
           "# '> ' is sent, '< ' is the reply the tool requires. Regenerate with:",
           "#   python3 -I ../tools/sdput.py transcript packs/%s > sdput_golden.txt" % name]
    for path, data in files:
        for line, want in put_steps(path, data):
            out.append("> " + line)
            out.append("< " + want)
    return "\n".join(out) + "\n"


def selftest():
    fails = 0

    def check(what, ok):
        nonlocal fails
        print(("  ok   " if ok else "  FAIL ") + what)
        fails += 0 if ok else 1

    check("crc32('123456789') == cbf43926", crc32(b"123456789") == 0xCBF43926)
    check("crc32(fox) == 414fa339", crc32(b"The quick brown fox jumps over the lazy dog") == 0x414FA339)
    check("crc32('') == 0", crc32(b"") == 0)
    check("hex is lower case, 2 digits a byte", chunk_line(5, b"\x00\xff\x7a") == "D 5 00ff7a")
    check("longest line fits", len(chunk_line(9727, b"\xab" * CHUNK)) <= LINE_CAP)
    for src, want in (("Midnight Ember", "midnight-ember"), ("  --My__Pack!! ", "my-pack"),
                      ("", "pack"), ("ÆTHER", "ther"), ("a" * 40, "a" * 31),
                      ("x" * 30 + " y", "x" * 30), ("COSMIC-CLONE", "cosmic-clone")):
        check("folder_name(%r) == %r" % (src, want), folder_name(src) == want)
    for f, ok in (("midnight-ember", True), ("My_Pack-2", True), ("a" * 31, True), ("a" * 32, False),
                  ("", False), ("..", False), ("a.b", False), ("a/b", False), ("CON", False),
                  ("com1", False), ("com10", True), ("console", True)):
        check("folder_ok(%r) is %s" % (f, ok), folder_ok(f) == ok)
    steps = put_steps("/themes/x/theme.json", bytes(range(200)))
    check("200 bytes: SDPUT, 3 chunks, END", len(steps) == 5 and steps[-1][0] == "END")
    check("chunk offsets 0/96/192", [s[1] for s in steps[1:4]] == ["OK 0", "OK 96", "OK 192"])
    print("\n%d FAILED" % fails if fails else "\nsdput.py selftest: all passed")
    return 1 if fails else 0


# --- the serial link ---------------------------------------------------------
class Link:
    def __init__(self, port, baud=115200, verbose=False):
        try:
            import serial  # pyserial; only the port commands need it
        except ImportError:
            raise SystemExit("pyserial missing: run with ~/.platformio/penv/bin/python")
        self.verbose = verbose
        s = serial.Serial()
        s.port = port
        s.baudrate = baud
        s.timeout = 0.05
        s.dtr = False            # set BEFORE open(): pyserial applies them at open,
        s.rts = False            # so it never raises the lines that reset the board
        s.open()
        self.s = s
        self.buf = b""
        # A garbage byte can arrive as the port opens; a lone newline ends
        # whatever partial line it started, so our first command is clean.
        time.sleep(0.3)
        self.s.write(b"\n")
        self.s.flush()
        time.sleep(0.3)
        self.s.reset_input_buffer()

    def close(self):
        self.s.close()

    def send(self, line):
        if self.verbose:
            print(">> " + (line if len(line) < 60 else line[:57] + "..."), file=sys.stderr)
        self.s.write(line.encode("ascii") + b"\n")
        self.s.flush()

    def lines(self, timeout):
        """Yield console lines until `timeout` seconds pass."""
        end = time.monotonic() + timeout
        while True:
            while b"\n" in self.buf:
                raw, self.buf = self.buf.split(b"\n", 1)
                line = raw.rstrip(b"\r").decode("ascii", "replace")
                if self.verbose and line:
                    print("<< " + line, file=sys.stderr)
                yield line
            if time.monotonic() >= end:
                return
            self.buf += self.s.read(256)

    def expect(self, want, timeout):
        """The confirming reply `want`, an 'ERR ...' line, or None on timeout.
        Every other line (the device's own log output) is ignored."""
        for line in self.lines(timeout):
            if line == want or line.startswith("ERR "):
                return line
        return None


def upload(link, path, data):
    steps = put_steps(path, data)
    line, want = steps[0]
    link.send(line)
    r = link.expect(want, 5.0)
    if r != want:
        print("%s: %s" % (path, r or "no reply to SDPUT"), file=sys.stderr)
        return False
    for line, want in steps[1:-1]:
        for attempt in range(1 + RETRIES):
            link.send(line)
            r = link.expect(want, 2.0)
            if r == want:
                break
            if r in ("ERR no upload",) or (r or "").startswith("ERR write") or r == "ERR upload timed out":
                print("%s: %s" % (path, r), file=sys.stderr)
                return False
            print("%s: chunk %s: %s, retry %d" % (path, want[3:], r or "no reply", attempt + 1), file=sys.stderr)
        else:
            print("%s: chunk %s failed %d times, giving up" % (path, want[3:], 1 + RETRIES), file=sys.stderr)
            link.send("ABORT")
            link.expect("OK aborted", 2.0)
            return False
    line, want = steps[-1]
    link.send(line)
    r = link.expect(want, 8.0)
    if r != want:
        print("%s: %s" % (path, r or "no reply to END"), file=sys.stderr)
        return False
    print("%s: %d bytes, crc %s, verified on the card" % (path, len(data), want.split()[2]))
    return True


def simple(link, line, want, timeout=5.0):
    link.send(line)
    r = link.expect(want, timeout)
    if r == want:
        print(r)
        return True
    print(r or "no reply", file=sys.stderr)
    return False


def listing(link, d):
    link.send("SDLS" + (" " + d if d else ""))
    ok = False
    for line in link.lines(5.0):
        if line.startswith("=== SDLS END"):
            return 0
        if line.startswith("=== SDLS"):
            ok = True
            continue
        if ok:
            print(line)
    return 0 if ok else 1


def main(argv):
    v = "-v" in argv
    argv = [a for a in argv if a != "-v"]
    if argv[:1] == ["selftest"]:
        return selftest()
    if argv[:1] == ["transcript"] and len(argv) == 2:
        sys.stdout.write(transcript(argv[1]))
        return 0
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 1
    port, cmd, args = argv[0], argv[1], argv[2:]
    want_args = {"put": 1, "del": 1, "rmdir": 1, "reboot": 0}
    if cmd == "ls" and len(args) <= 1:
        pass
    elif want_args.get(cmd) != len(args):
        print(__doc__, file=sys.stderr)
        return 1
    if cmd == "put":
        try:
            name, files = pack_files(args[0])
        except (ValueError, OSError) as e:
            print("put: %s" % e, file=sys.stderr)
            return 1
        if name != os.path.basename(os.path.normpath(args[0])):
            print("note: installing as /themes/%s (the designer's folder rule)" % name)
    try:
        link = Link(port, verbose=v)
    except OSError as e:          # serial.SerialException is an OSError
        print("cannot open %s: %s" % (port, e), file=sys.stderr)
        return 1
    try:
        if cmd == "put":
            for path, data in files:
                if not upload(link, path, data):
                    return 1
            print("installed /themes/%s -- packs load at boot: sdput.py %s reboot" % (name, port))
            return 0
        if cmd == "del":
            p = args[0] if args[0].startswith("/") else "/" + args[0]
            return 0 if simple(link, "SDDEL " + p, "OK") else 1
        if cmd == "rmdir":
            return 0 if simple(link, "SDRMDIR " + args[0], "OK") else 1
        if cmd == "reboot":
            return 0 if simple(link, "REBOOT", "OK rebooting") else 1
        return listing(link, args[0] if args else "")
    finally:
        link.close()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
