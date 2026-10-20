#!/usr/bin/env python3
"""Save UMBRAVOX's wardrive log as a WiGLE file.

    python3 tools/wigle_dump.py /dev/ttyUSB0
    python3 tools/wigle_dump.py /dev/ttyUSB0 -o trip.csv

The Core writes a WigleWifi-1.6 CSV between two marker lines when it is sent
WIGLE on its console. Everything else the firmware prints -- the per-frame
status line, mesh chatter -- keeps going while that happens, so this keeps
only the rows between the markers and drops anything that interleaved.

The result uploads to https://wigle.net/uploads as it is.
"""
import argparse, sys, time

try:
    import serial
except ImportError:
    sys.exit("pyserial is needed:  pip install pyserial")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("-o", "--out")
    ap.add_argument("--timeout", type=float, default=20.0,
                    help="seconds of silence before giving up")
    args = ap.parse_args()

    s = serial.Serial(args.port, args.baud, timeout=1)
    time.sleep(0.3)
    s.reset_input_buffer()
    s.write(b"WIGLE\n")
    s.flush()

    rows, header, inside, summary = [], None, False, ""
    last = time.time()
    while time.time() - last < args.timeout:
        raw = s.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", "replace").rstrip()
        if line.startswith("=== WIGLE BEGIN"):
            inside = True
            last = time.time()
            continue
        if line.startswith("=== WIGLE END"):
            summary = line
            break
        if not inside:
            continue
        last = time.time()
        if line.startswith("WigleWifi-1.6,"):
            header = line
        elif line.startswith("MAC,SSID,"):
            rows.append(line)          # column header, kept in order
        elif "," in line and line.count(",") >= 10:
            # A status line can land mid-dump; a real row starts with a MAC.
            first = line.split(",", 1)[0]
            if len(first) == 17 and first.count(":") == 5:
                rows.append(line)
    s.close()

    if not header:
        print("no WiGLE header came back; nothing saved", file=sys.stderr)
        return 1

    data = [r for r in rows if not r.startswith("MAC,SSID,")]
    if not data:
        print("header but no rows -- nothing has a position yet?", file=sys.stderr)
        print("check 'loc N/M' in the status line", file=sys.stderr)
        return 1

    out = args.out or time.strftime("umbravox-%Y%m%d-%H%M%S.csv")
    with open(out, "w", newline="") as f:
        f.write(header + "\n")
        f.write("MAC,SSID,AuthMode,FirstSeen,Channel,RSSI,CurrentLatitude,"
                "CurrentLongitude,AltitudeMeters,AccuracyMeters,Type\n")
        for r in data:
            f.write(r + "\n")

    print("%s  (%d networks)" % (out, len(data)))
    if summary:
        print(summary.replace("=== WIGLE END === ", ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
