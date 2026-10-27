// The device's SD-install console on stdin/stdout, for testing a host client
// (web/designer/js/usbinstall.js) against the REAL state machine without a
// board. Bytes in on stdin, through sdcmd::LineBuf and sdcmd::Console exactly
// as device/main.cpp pollConsole() feeds them, onto the in-memory card of
// sim/fakesd.h; replies out on stdout with CRLF, as Serial.println sends.
// SDLS prints the listing format of device/sdlog.cpp list().
//
//   sdcmd_console [--banner] [--status-ms N] [--mkdir DIR]... [--put PATH=HOSTFILE]...
//                 [--drop-ok N]... [--dump OUTDIR]
//
//   --banner      print boot text (with a garbage byte) before the first prompt
//   --status-ms   print a device-like status line every N ms, between replies
//   --drop-ok N   swallow the Nth "OK <offset>" reply (a reply lost on the wire)
//   --dump        at EOF on stdin write every card file under OUTDIR/<path>,
//                 the card's directories to OUTDIR/.dirs and every line the
//                 console received to OUTDIR/.received
//
// Host-only test rig: it is not built into the firmware.
#include "fakesd.h"
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <set>
#include <string>
#include <vector>

static uint32_t nowMs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

static void out(const char* s) {
    std::string l(s);
    l += "\r\n";
    size_t o = 0;
    while (o < l.size()) {
        const ssize_t k = write(1, l.data() + o, l.size() - o);
        if (k < 0) { if (errno == EINTR) continue; exit(2); }
        o += (size_t)k;
    }
}

// device/sdlog.cpp list(): "   <dir>  name/" and "%8lu  name".
static void listing(FakeSd& sd, const char* dir) {
    char l[256];
    snprintf(l, sizeof l, "=== SDLS %s ===", dir);
    out(l);
    const std::string k = key(dir);
    if (!sd.card) out("sdlog: no card");
    else if (!sd.dirs.count(k)) { snprintf(l, sizeof l, "sdlog: cannot open %s", dir); out(l); }
    else {
        for (const auto& d : sd.dirs)
            if (d != k && parentOf(d) == k) { snprintf(l, sizeof l, "   <dir>  %s/", d.substr(d.rfind('/') + 1).c_str()); out(l); }
        for (const auto& f : sd.files)
            if (parentOf(f.first) == k) {
                snprintf(l, sizeof l, "%8lu  %s", (unsigned long)f.second.size(), f.first.substr(f.first.rfind('/') + 1).c_str());
                out(l);
            }
    }
    out("=== SDLS END ===");
}

static bool mkdirs(const std::string& p) {
    for (size_t i = 1; i <= p.size(); i++)
        if (i == p.size() || p[i] == '/') {
            const std::string s = p.substr(0, i);
            if (::mkdir(s.c_str(), 0700) != 0 && errno != EEXIST) return false;
        }
    return true;
}

static bool dump(FakeSd& sd, const std::string& dir, const std::vector<std::string>& received) {
    if (!mkdirs(dir)) return false;
    for (const auto& f : sd.files) {
        const std::string p = dir + f.first;
        if (!mkdirs(p.substr(0, p.rfind('/')))) return false;
        FILE* o = fopen(p.c_str(), "wb");
        if (!o) return false;
        if (!f.second.empty()) fwrite(f.second.data(), 1, f.second.size(), o);
        fclose(o);
    }
    FILE* d = fopen((dir + "/.dirs").c_str(), "w");
    if (!d) return false;
    for (const auto& x : sd.dirs) fprintf(d, "%s\n", x.c_str());
    fclose(d);
    FILE* r = fopen((dir + "/.received").c_str(), "w");
    if (!r) return false;
    for (const auto& x : received) fprintf(r, "%s\n", x.c_str());
    fclose(r);
    return true;
}

static void usage() {
    fprintf(stderr, "usage: sdcmd_console [--banner] [--status-ms N] [--mkdir DIR]... "
                    "[--put PATH=HOSTFILE]... [--drop-ok N]... [--dump OUTDIR]\n");
    exit(1);
}

int main(int argc, char** argv) {
    FakeSd sd;
    bool banner = false;
    uint32_t statusMs = 0;
    std::set<unsigned> dropOk;
    std::string dumpDir;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--banner") banner = true;
        else if (a == "--status-ms" && more) statusMs = (uint32_t)atoi(argv[++i]);
        else if (a == "--drop-ok" && more) dropOk.insert((unsigned)atoi(argv[++i]));
        else if (a == "--dump" && more) dumpDir = argv[++i];
        else if (a == "--mkdir" && more) {
            const std::string p = argv[++i];
            for (size_t j = 1; j <= p.size(); j++)
                if (j == p.size() || p[j] == '/') sd.mkdir(p.substr(0, j).c_str());
        } else if (a == "--put" && more) {
            const std::string s = argv[++i];
            const size_t eqAt = s.find('=');
            if (eqAt == std::string::npos) usage();
            const std::string to = s.substr(0, eqAt), from = s.substr(eqAt + 1);
            FILE* f = fopen(from.c_str(), "rb");
            if (!f) { perror(from.c_str()); return 1; }
            std::vector<uint8_t> d;
            uint8_t b[4096];
            size_t k;
            while ((k = fread(b, 1, sizeof b, f)) > 0) d.insert(d.end(), b, b + k);
            fclose(f);
            const std::string parent = to.substr(0, to.rfind('/'));
            for (size_t j = 1; j <= parent.size(); j++)
                if (j == parent.size() || parent[j] == '/') sd.mkdir(parent.substr(0, j).c_str());
            sd.put(to.c_str(), d);
        } else usage();
    }

    sdcmd::Console con(sd);
    sdcmd::LineBuf lb;
    std::vector<std::string> received;
    char reply[sdcmd::REPLY_CAP];
    unsigned oks = 0;
    uint32_t frame = 0, lastStatus = nowMs();

    if (banner) {
        const unsigned char junk[] = {0xFF, 0x00, 0xFE, '\r', '\n'};
        if (write(1, junk, sizeof junk) < 0) return 2;
        out("ets Jun  8 2016 00:22:57");
        out("rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)");
        out("");
        out("UMBRAVOX bring-up");
        out("chip  : ESP32-D0WD-V3 rev 3, 2 core(s)");
        out("panel up @ 40 MHz SPI, GPS on GPIO 22 @ 9600");
    }

    for (;;) {
        pollfd p{0, POLLIN, 0};
        const int pr = poll(&p, 1, 5);
        const uint32_t now = nowMs();
        if (con.tick(now, reply, sizeof reply)) out(reply);
        if (statusMs && now - lastStatus >= statusMs) {
            lastStatus = now;
            char s[200];
            frame += 30;
            snprintf(s, sizeof s, "f%-5u [scan ] draw 12 blit  3 ms | wifi   41/512  (2.4 30, 5 11) 2 fr ch6   ok | "
                                  "ble   12/2048   | gps no   0 sv | heap 81234", (unsigned)frame);
            out(s);
        }
        if (pr < 0) { if (errno == EINTR) continue; return 2; }
        if (pr == 0) continue;
        unsigned char buf[256];
        const ssize_t n = read(0, buf, sizeof buf);
        if (n < 0) { if (errno == EINTR) continue; return 2; }
        if (n == 0) break;                                      // EOF: the host hung up
        for (ssize_t i = 0; i < n; i++) {
            const sdcmd::LineBuf::Kind k = lb.feed((char)buf[i]);
            if (k == sdcmd::LineBuf::NONE) continue;
            if (k == sdcmd::LineBuf::OVERLONG) { received.push_back("<overlong>"); out("ERR line too long (max 200)"); continue; }
            if (k == sdcmd::LineBuf::BADBYTE) {
                received.push_back("<badbyte>");
                out(con.uploading() ? "ERR non-printable byte in line" : "unknown command (non-printable bytes, ignored)");
                continue;
            }
            const char* line = lb.buf;
            received.push_back(line);
            const sdcmd::Action a = con.line(line, nowMs(), "", reply, sizeof reply);
            if (a == sdcmd::REBOOT) { out(reply); continue; }
            if (a == sdcmd::HANDLED) {
                const bool isOk = !strncmp(reply, "OK ", 3) && reply[3] >= '0' && reply[3] <= '9';
                if (isOk && dropOk.count(++oks)) continue;      // lost on the wire
                out(reply);
                continue;
            }
            // device/main.cpp's own table, as far as a host client can reach it
            if (!strncmp(line, "SDLS", 4)) {
                const char* d = line + 4;
                while (*d == ' ') d++;
                listing(sd, *d ? d : "/");
            } else {
                char u[sdcmd::LINE_CAP + 64];
                snprintf(u, sizeof u, "unknown command '%s'", line);
                out(u);
            }
        }
    }
    if (!dumpDir.empty() && !dump(sd, dumpDir, received)) { fprintf(stderr, "sdcmd_console: cannot write %s\n", dumpDir.c_str()); return 3; }
    return 0;
}
