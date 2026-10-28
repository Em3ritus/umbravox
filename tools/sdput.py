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
import stat
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


def read_pack_file(folder_real, fname, cap):
    """The bytes of <folder_real>/<fname>, or None if it does not exist.

    Pack folders come out of zips anyone can make, so a file is read only if
    it is a REGULAR file that really sits in the folder: a symlink (even to a
    file inside the pack), a FIFO, a device or a directory is refused, and so
    is a file that changes between the check and the read. The size cap is
    checked before anything is read, let alone sent."""
    path = os.path.join(folder_real, fname)
    try:
        st = os.lstat(path)
    except FileNotFoundError:
        return None
    if not stat.S_ISREG(st.st_mode):
        raise ValueError("%s is not a regular file (a symlink, FIFO, device or "
                         "directory): refused" % path)
    if st.st_nlink > 1:
        raise ValueError("%s has %d hard links (it may be a link to a file outside "
                         "the pack): refused" % (path, st.st_nlink))
    if os.path.dirname(os.path.realpath(path)) != folder_real:
        raise ValueError("%s resolves outside the pack folder: refused" % path)
    if st.st_size == 0:
        raise ValueError("%s is empty" % path)
    if st.st_size > cap:
        raise ValueError("%s is over %d bytes" % (path, cap))
    # O_NOFOLLOW: a symlink swapped in after the lstat fails to open (ELOOP);
    # O_NONBLOCK: a FIFO swapped in cannot hang the open. Then fstat the very
    # file that was opened and require it to be the one lstat saw.
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    try:
        fd = os.open(path, flags)
    except OSError as e:
        raise ValueError("%s changed while it was being read (%s): refused" % (path, e.strerror))
    with os.fdopen(fd, "rb") as fh:
        ft = os.fstat(fh.fileno())
        if (not stat.S_ISREG(ft.st_mode) or ft.st_ino != st.st_ino or ft.st_dev != st.st_dev
                or ft.st_size != st.st_size):
            raise ValueError("%s changed while it was being read: refused" % path)
        data = fh.read(cap + 1)
    if len(data) != st.st_size:
        raise ValueError("%s changed while it was being read: refused" % path)
    return data


def pack_files(folder):
    """[(device path, bytes)] for a pack folder: emblem.bin first, so a
    theme.json that needs it never lands on the card without it.

    The folder ARGUMENT itself is followed if it is a symlink (you typed it),
    and the pack is named after it as typed; no file INSIDE it is ever
    followed (read_pack_file)."""
    folder = os.path.normpath(folder)
    name = folder_name(os.path.basename(folder))
    if not folder_ok(name):
        raise ValueError("folder name %r is not usable on the device" % name)
    real = os.path.realpath(folder)
    if not os.path.isdir(real):
        raise ValueError("%s is not a folder" % folder)
    emblem = read_pack_file(real, "emblem.bin", MAX_EMBLEM)
    theme = read_pack_file(real, "theme.json", MAX_JSON)
    if theme is None:
        raise ValueError("%s has no theme.json" % folder)
    files = []
    if emblem is not None:
        files.append(("/themes/%s/emblem.bin" % name, emblem))
    files.append(("/themes/%s/theme.json" % name, theme))
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
    selftest_files(check)
    for a, ok in (("/WD-0001.csv", True), ("/themes/x/theme.json", True), ("/thèmes/x", False),
                  ("caf\u00e9", False), ("a b", False), ("a\tb", False), ("", False), ("x" * 121, False)):
        check("arg_ok(%r) is %s" % (a, ok), arg_ok(a) == ok)

    class FakeLink:                       # answers SDPUT and the first chunk, then Ctrl-C
        def __init__(self):
            self.sent = []
        def send(self, line):
            self.sent.append(line)
            if len(self.sent) == 3:
                raise KeyboardInterrupt
        def expect(self, want, timeout):
            return want
    fl = FakeLink()
    devnull = open(os.devnull, "w")
    old_err, sys.stderr = sys.stderr, devnull
    try:
        r = put_all(fl, [("/themes/x/theme.json", bytes(300))])
    finally:
        sys.stderr = old_err
        devnull.close()
    check("Ctrl-C mid-put: returns failure and sends ABORT", r is False and fl.sent[-1] == "ABORT")      # counts through check(); not `fails += ...`, which reads fails first
    print("\n%d FAILED" % fails if fails else "\nsdput.py selftest: all passed")
    return 1 if fails else 0


def selftest_files(check):
    """pack_files() on hostile pack folders: nothing but regular files that
    really sit in the folder is ever read, and the caps hold before sending."""
    import tempfile

    def refused(folder, what):
        try:
            pack_files(folder)
        except ValueError as e:
            return what in str(e)
        return False

    with tempfile.TemporaryDirectory() as td:
        secret = os.path.join(td, "id_ed25519")
        with open(secret, "wb") as fh:
            fh.write(b"-----BEGIN OPENSSH PRIVATE KEY-----\n")

        def pack(name, theme=b"{}\n", emblem=None):
            d = os.path.join(td, name)
            os.mkdir(d)
            if theme is not None:
                with open(os.path.join(d, "theme.json"), "wb") as fh:
                    fh.write(theme)
            if emblem is not None:
                with open(os.path.join(d, "emblem.bin"), "wb") as fh:
                    fh.write(emblem)
            return d

        good = pack("good-pack", emblem=b"UVE\x01")
        name, files = pack_files(good)
        check("a plain pack is read", name == "good-pack" and [f[0] for f in files] ==
              ["/themes/good-pack/emblem.bin", "/themes/good-pack/theme.json"])

        d = pack("sym-theme", theme=None)
        os.symlink(secret, os.path.join(d, "theme.json"))
        check("symlinked theme.json refused", refused(d, "not a regular file"))

        d = pack("sym-emblem")
        os.symlink(secret, os.path.join(d, "emblem.bin"))
        check("symlinked emblem.bin refused", refused(d, "not a regular file"))

        d = pack("sym-inside")
        os.symlink("theme.json", os.path.join(d, "emblem.bin"))   # even to a file in the pack
        check("symlink to a file inside the pack refused", refused(d, "not a regular file"))

        d = pack("hard-emblem")
        os.link(secret, os.path.join(d, "emblem.bin"))
        check("hard-linked emblem.bin refused", refused(d, "hard link"))

        d = pack("fifo-theme", theme=None)
        os.mkfifo(os.path.join(d, "theme.json"))
        check("FIFO theme.json refused (and never opened)", refused(d, "not a regular file"))

        d = pack("dir-emblem")
        os.mkdir(os.path.join(d, "emblem.bin"))
        check("a directory named emblem.bin refused", refused(d, "not a regular file"))

        d = pack("big-theme", theme=b" " * (MAX_JSON + 1))
        check("theme.json over 4096 bytes refused before sending", refused(d, "over 4096"))
        d = pack("big-emblem", emblem=b"\0" * (MAX_EMBLEM + 1))
        check("emblem.bin over 9728 bytes refused before sending", refused(d, "over 9728"))
        d = pack("empty-theme", theme=b"")
        check("empty theme.json refused", refused(d, "empty"))

        link = os.path.join(td, "linked-pack")
        os.symlink(good, link)
        try:
            name, files = pack_files(link)
            ok = name == "linked-pack" and len(files) == 2
        except ValueError:
            ok = False
        check("a symlinked FOLDER argument is followed, named as typed", ok)

        # The race: lstat sees a regular file, open() sees something else.
        d = pack("swap-race")
        real_open = os.open

        def swapping_open(path, flags, *a):
            if path.endswith("theme.json"):
                os.remove(path)
                os.symlink(secret, path)
            return real_open(path, flags, *a)
        os.open = swapping_open
        try:
            check("a file swapped for a symlink after lstat refused", refused(d, "changed"))
        finally:
            os.open = real_open


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


def put_all(link, files):
    """Upload each file in turn. On Ctrl-C or any error mid-way, tell the
    device to ABORT before giving up, so it removes the temp at once rather
    than holding the console for its 10 s idle timeout."""
    try:
        for path, data in files:
            if not upload(link, path, data):
                return False
        return True
    except (KeyboardInterrupt, Exception) as e:
        what = "interrupted" if isinstance(e, KeyboardInterrupt) else "error: %s" % e
        try:
            link.send("ABORT")
            link.expect("OK aborted", 2.0)
        except (KeyboardInterrupt, Exception):
            pass
        print("put %s: sent ABORT; the card keeps the files it had" % what, file=sys.stderr)
        return False


def arg_ok(a):
    """del/rmdir/ls arguments go on the wire as one ASCII token."""
    return bool(a) and a.isascii() and a.isprintable() and " " not in a and len(a) <= 120


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
    for a in args if cmd in ("del", "rmdir", "ls") else ():
        if not arg_ok(a):
            print("%s: %r is not usable: paths are printable ASCII with no spaces" % (cmd, a), file=sys.stderr)
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
            if not put_all(link, files):
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
    try:
        sys.exit(main(sys.argv[1:]))
    except KeyboardInterrupt:      # outside a put (put_all sends ABORT itself)
        print("interrupted", file=sys.stderr)
        sys.exit(1)
