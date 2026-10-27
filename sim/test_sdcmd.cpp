// Host tests for the serial SD commands (device/sdcmd.h): the path policy,
// the line framing and strict decoders, and the upload state machine against
// an in-memory FAT-like card (case-insensitive names, injectable failures).
// The last section replays a transcript written by tools/sdput.py, so the
// host encoder and the device decoder are checked byte for byte.
#include "../device/sdcmd.h"
#include <stdio.h>
#include <string.h>
#include <map>
#include <set>
#include <string>
#include <vector>

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

// --- a fake card ----------------------------------------------------------
// FAT compares names case-insensitively, so every key is lower-cased.
static bool eq(const char* a, const char* b) { return a && b && !strcmp(a, b); }
static bool has(const char* a, const char* b) { return a && b && strstr(a, b) != nullptr; }

static std::string key(const char* p) {
    std::string s(p ? p : "");
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    return s;
}
static std::string parentOf(const std::string& k) {
    const size_t i = k.rfind('/');
    return i == 0 ? std::string("/") : k.substr(0, i);
}

struct FakeSd : sdcmd::Io {
    std::map<std::string, std::vector<uint8_t>> files;
    std::set<std::string> dirs{"/"};
    bool card = true;
    int  failWriteAfter = -1;     // appends allowed before every append fails
    bool failRename = false, failMkdir = false, failOpen = false, failClose = false;
    size_t capacity = 1u << 20;   // bytes on the card (a full card fails short)
    std::string tmpKey;
    bool tmpOpenNow = false;
    int  appends = 0;
    std::vector<std::string> notes;

    size_t used() const { size_t u = 0; for (auto& f : files) u += f.second.size(); return u; }

    bool present() override { return card; }
    bool exists(const char* p) override { const std::string k = key(p); return card && (files.count(k) || dirs.count(k)); }
    bool isDir(const char* p) override { return card && dirs.count(key(p)); }
    bool mkdir(const char* p) override {
        const std::string k = key(p);
        if (!card || failMkdir || files.count(k) || dirs.count(k) || !dirs.count(parentOf(k))) return false;
        dirs.insert(k);
        return true;
    }
    bool remove(const char* p) override {
        const std::string k = key(p);
        if (!card || !files.count(k) || (tmpOpenNow && k == tmpKey)) return false;
        files.erase(k);
        return true;
    }
    bool rmdir(const char* p) override {
        const std::string k = key(p);
        if (!card || !dirs.count(k) || k == "/") return false;
        for (auto& f : files) if (parentOf(f.first) == k) return false;
        for (auto& d : dirs)  if (d != k && parentOf(d) == k) return false;
        dirs.erase(k);
        return true;
    }
    bool rename(const char* from, const char* to) override {
        const std::string a = key(from), b = key(to);
        if (!card || failRename || !files.count(a) || files.count(b) || dirs.count(b)) return false;
        files[b] = files[a];
        files.erase(a);
        return true;
    }
    bool tmpOpen(const char* p) override {
        const std::string k = key(p);
        if (!card || failOpen || tmpOpenNow || dirs.count(k) || !dirs.count(parentOf(k))) return false;
        files[k].clear();
        tmpKey = k; tmpOpenNow = true;
        return true;
    }
    bool tmpAppend(const uint8_t* d, size_t n) override {
        if (!card || !tmpOpenNow) return false;
        if (failWriteAfter >= 0 && appends >= failWriteAfter) return false;
        if (used() + n > capacity) {               // a full card: a short write
            const size_t room = capacity - used();
            files[tmpKey].insert(files[tmpKey].end(), d, d + room);
            return false;
        }
        appends++;
        files[tmpKey].insert(files[tmpKey].end(), d, d + n);
        return true;
    }
    long tmpRead(uint32_t off, uint8_t* buf, size_t n) override {
        if (!card || !tmpOpenNow) return -1;
        const auto& f = files[tmpKey];
        if (off > f.size()) return -1;
        const size_t k = f.size() - off < n ? f.size() - off : n;
        memcpy(buf, f.data() + off, k);
        return (long)k;
    }
    bool tmpClose() override {
        if (!tmpOpenNow) return false;
        tmpOpenNow = false;
        return card && !failClose;
    }
    bool crcOf(const char* p, uint32_t& size, uint32_t& crc) override {
        const std::string k = key(p);
        if (!card || !files.count(k)) return false;
        const auto& f = files[k];
        size = (uint32_t)f.size();
        crc = sdcmd::crc32(0, f.data(), f.size());
        return true;
    }
    void note(bool warn, const char* msg) override {
        notes.push_back(std::string(warn ? "WARN " : "INFO ") + msg);
    }

    bool has(const char* p) const { return files.count(key(p)) != 0; }
    std::vector<uint8_t> get(const char* p) const { auto it = files.find(key(p)); return it == files.end() ? std::vector<uint8_t>() : it->second; }
    void put(const char* p, const std::vector<uint8_t>& d) { files[key(p)] = d; }
};

static std::vector<uint8_t> bytes(size_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; i++) v[i] = (uint8_t)(i * 37u + seed);
    return v;
}
static std::string hexOf(const uint8_t* p, size_t n) {
    static const char* H = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) { s += H[p[i] >> 4]; s += H[p[i] & 15]; }
    return s;
}

// One console + card, and a helper that sends a line and returns the reply.
struct Rig {
    FakeSd sd;
    sdcmd::Console con{sd};
    char reply[sdcmd::REPLY_CAP];
    uint32_t now = 1000;
    const char* active = "";
    sdcmd::Action last = sdcmd::NOT_MINE;
    std::string send(const std::string& line) {
        reply[0] = 0;
        last = con.line(line.c_str(), now, active, reply, sizeof reply);
        return reply;
    }
    std::string sdput(const char* path, const std::vector<uint8_t>& d) {
        char l[160];
        snprintf(l, sizeof l, "SDPUT %s %u %08x", path, (unsigned)d.size(),
                 (unsigned)sdcmd::crc32(0, d.data(), d.size()));
        return send(l);
    }
    std::string chunk(uint32_t off, const std::vector<uint8_t>& d, size_t from, size_t n) {
        return send("D " + std::to_string(off) + " " + hexOf(d.data() + from, n));
    }
    // A whole upload; returns the END reply ("" if a step before it failed).
    std::string upload(const char* path, const std::vector<uint8_t>& d) {
        if (sdput(path, d).rfind("OK", 0) != 0) return "";
        for (size_t o = 0; o < d.size(); o += sdcmd::CHUNK_MAX) {
            const size_t n = d.size() - o < sdcmd::CHUNK_MAX ? d.size() - o : sdcmd::CHUNK_MAX;
            if (chunk((uint32_t)o, d, o, n) != "OK " + std::to_string(o)) return "";
        }
        return send("END");
    }
};

static std::string doneFor(const std::vector<uint8_t>& d) {
    char b[48];
    snprintf(b, sizeof b, "DONE %u %08x", (unsigned)d.size(), (unsigned)sdcmd::crc32(0, d.data(), d.size()));
    return b;
}

// --- policy tables --------------------------------------------------------
static void testPutPolicy() {
    printf("put policy:\n");
    struct Row { const char* path; uint32_t size; bool ok; const char* canon; };
    const Row rows[] = {
        {"/themes/midnight-ember/theme.json", 758,  true,  "/themes/midnight-ember/theme.json"},
        {"/themes/midnight-ember/emblem.bin", 9728, true,  "/themes/midnight-ember/emblem.bin"},
        {"/themes/a/theme.json",              1,    true,  "/themes/a/theme.json"},
        {"/themes/My_Pack-2/THEME.JSON",      100,  true,  "/themes/My_Pack-2/theme.json"},
        {"/THEMES/x/Emblem.Bin",              100,  true,  "/themes/x/emblem.bin"},
        {"/themes/x/theme.json",              4096, true,  "/themes/x/theme.json"},
        {"/themes/x/theme.json",              4097, false, nullptr},          // over MAX_JSON
        {"/themes/x/emblem.bin",              9729, false, nullptr},          // over MAX_EMBLEM
        {"/themes/x/theme.json",              0,    false, nullptr},          // empty
        {"/themes/x/theme.json",              4294967295u, false, nullptr},
        {"/themes/../theme.json",             10,   false, nullptr},
        {"/themes/x/../theme.json",           10,   false, nullptr},
        {"/themes/../../diag.log",            10,   false, nullptr},
        {"/themes/..",                        10,   false, nullptr},
        {"/themes//theme.json",               10,   false, nullptr},
        {"/themes/x//theme.json",             10,   false, nullptr},
        {"//themes/x/theme.json",             10,   false, nullptr},
        {"/themes/x/theme.json/",             10,   false, nullptr},
        {"themes/x/theme.json",               10,   false, nullptr},          // relative
        {"/themes/x/y/theme.json",            10,   false, nullptr},          // too deep
        {"/themes/x/theme.json.tmp",          10,   false, nullptr},          // temp is ours
        {"/themes/x/emblem.bin.old",          10,   false, nullptr},
        {"/themes/x/preview.png",             10,   false, nullptr},
        {"/themes/x/theme.jso",               10,   false, nullptr},
        {"/themes/x/theme.jsonx",             10,   false, nullptr},
        {"/themes/x",                         10,   false, nullptr},
        {"/themes/theme.json",                10,   false, nullptr},
        {"/themes/a.b/theme.json",            10,   false, nullptr},          // folderNameOk: no dots
        {"/themes/a b/theme.json",            10,   false, nullptr},
        {"/themes/a\\b/theme.json",           10,   false, nullptr},
        {"/themes/0123456789012345678901234567890/theme.json", 10, true,
         "/themes/0123456789012345678901234567890/theme.json"},             // 31 chars
        {"/themes/01234567890123456789012345678901/theme.json", 10, false, nullptr}, // 32
        {"/themes/CON/theme.json",            10,   false, nullptr},          // DOS device names
        {"/themes/nul/theme.json",            10,   false, nullptr},
        {"/themes/Com1/theme.json",           10,   false, nullptr},
        {"/themes/lpt9/emblem.bin",           10,   false, nullptr},
        {"/themes/aux/theme.json",            10,   false, nullptr},
        {"/themes/console/theme.json",        10,   true,  "/themes/console/theme.json"},
        {"/themes/com10/theme.json",          10,   true,  "/themes/com10/theme.json"},
        {"/diag.log",                         10,   false, nullptr},
        {"/perf.csv",                         10,   false, nullptr},
        {"/WD-0001.csv",                      10,   false, nullptr},          // logs are never written
        {"/themes2/x/theme.json",             10,   false, nullptr},
        {"/themesx/theme.json",               10,   false, nullptr},
        {"",                                  10,   false, nullptr},
        {"/",                                 10,   false, nullptr},
    };
    for (const Row& r : rows) {
        sdcmd::Path p;
        const char* why = sdcmd::putPolicy(r.path, r.size, p);
        char what[200];
        snprintf(what, sizeof what, "%s %u -> %s", r.path, (unsigned)r.size, r.ok ? "allowed" : "refused");
        bool good = r.ok ? (why == nullptr && eq(p.canon, r.canon)) : (why != nullptr);
        check(what, good);
    }
    sdcmd::Path p;
    check("null path refused", sdcmd::putPolicy(nullptr, 10, p) != nullptr);
    char longp[300];
    memset(longp, 'a', sizeof longp); longp[0] = '/'; longp[299] = 0;
    check("300-char path refused", sdcmd::putPolicy(longp, 10, p) != nullptr);
    check("over-cap reason names the size",
          has(sdcmd::putPolicy("/themes/x/theme.json", 5000, p), "too large"));
    check("bad path reason is 'path not allowed'",
          eq(sdcmd::putPolicy("/diag.log", 10, p), "path not allowed"));
}

static void testDelPolicy() {
    printf("delete policy:\n");
    struct Row { const char* path; const char* active; bool ok; const char* canon; };
    const char* A = "/WD-20260119-120000.csv";
    const Row rows[] = {
        {"/themes/x/theme.json",            A,  true,  "/themes/x/theme.json"},
        {"/themes/x/EMBLEM.BIN",            A,  true,  "/themes/x/emblem.bin"},
        {"/themes/x/theme.json.tmp",        A,  true,  "/themes/x/theme.json.tmp"},  // a power-cut leftover
        {"/themes/x/emblem.bin.old",        A,  true,  "/themes/x/emblem.bin.old"},
        {"/themes/x/preview.png",           A,  false, nullptr},
        {"/themes/x",                       A,  false, nullptr},                     // folders: SDRMDIR
        {"/themes/../diag.log",             A,  false, nullptr},
        {"/themes/x/../../diag.log",        A,  false, nullptr},
        {"/WD-0001.csv",                    A,  true,  "/WD-0001.csv"},
        {"/WD-0001.CSV",                    A,  true,  "/WD-0001.CSV"},              // older firmware
        {"/wd-0001.csv",                    A,  true,  "/wd-0001.csv"},
        {"/WD-20260118-090000.csv",         A,  true,  "/WD-20260118-090000.csv"},
        {"/WD-20260119-120000.csv",         A,  false, nullptr},                     // the live session
        {"/wd-20260119-120000.CSV",         A,  false, nullptr},                     // ...in any case
        {"/WD-20260119-120000.csv",         "", true,  "/WD-20260119-120000.csv"},   // idle: nothing live
        {"/WD-20260119-120000.csv",         nullptr, true, "/WD-20260119-120000.csv"},
        {"/WD-.csv",                        A,  false, nullptr},
        {"/WD-0001.csv.tmp",                A,  false, nullptr},                     // no tmp outside themes
        {"/WD-0001.txt",                    A,  false, nullptr},
        {"/WD-00x1.csv",                    A,  false, nullptr},
        {"/WD-0001.csv/",                   A,  false, nullptr},
        {"/sub/WD-0001.csv",                A,  false, nullptr},
        {"WD-0001.csv",                     A,  false, nullptr},
        {"//WD-0001.csv",                   A,  false, nullptr},
        {"/diag.log",                       A,  false, nullptr},
        {"/diag.0.log",                     A,  false, nullptr},
        {"/perf.csv",                       A,  false, nullptr},
        {"/x.tmp",                          A,  false, nullptr},
        {"/System Volume Information",      A,  false, nullptr},
        {"/themes",                         A,  false, nullptr},
        {"/",                               A,  false, nullptr},
        {"",                                A,  false, nullptr},
    };
    for (const Row& r : rows) {
        sdcmd::Path p;
        const char* why = sdcmd::delPolicy(r.path, r.active, p);
        char what[200];
        snprintf(what, sizeof what, "del %s (live %s) -> %s", r.path, r.active ? r.active : "null",
                 r.ok ? "allowed" : "refused");
        check(what, r.ok ? (why == nullptr && eq(p.canon, r.canon)) : why != nullptr);
    }
    sdcmd::Path p;
    check("live session reason says so", has(sdcmd::delPolicy(A, A, p), "active"));

    printf("rmdir policy:\n");
    check("rmdir midnight-ember", sdcmd::rmdirPolicy("midnight-ember", p) == nullptr &&
                                  eq(p.canon, "/themes/midnight-ember"));
    check("rmdir ..",            sdcmd::rmdirPolicy("..", p) != nullptr);
    check("rmdir x/y",           sdcmd::rmdirPolicy("x/y", p) != nullptr);
    check("rmdir /themes/x",     sdcmd::rmdirPolicy("/themes/x", p) != nullptr);
    check("rmdir empty",         sdcmd::rmdirPolicy("", p) != nullptr);
    check("rmdir CON",           sdcmd::rmdirPolicy("CON", p) != nullptr);
}

// --- decoders and framing -------------------------------------------------
static void testDecoders() {
    printf("crc32 (zlib / IEEE):\n");
    const char* v = "123456789";
    check("crc32('123456789') == cbf43926", sdcmd::crc32(0, (const uint8_t*)v, 9) == 0xCBF43926u);
    check("crc32('') == 0", sdcmd::crc32(0, nullptr, 0) == 0);
    const uint32_t part = sdcmd::crc32(0, (const uint8_t*)v, 4);
    check("crc32 is incremental", sdcmd::crc32(part, (const uint8_t*)v + 4, 5) == 0xCBF43926u);
    const char* fox = "The quick brown fox jumps over the lazy dog";
    check("crc32(fox) == 414fa339", sdcmd::crc32(0, (const uint8_t*)fox, strlen(fox)) == 0x414FA339u);

    printf("hex decode:\n");
    uint8_t out[sdcmd::CHUNK_MAX];
    check("00ff7A -> 3 bytes", sdcmd::hexDecode("00ff7A", 6, out, sizeof out) == 3 &&
                               out[0] == 0 && out[1] == 0xFF && out[2] == 0x7A);
    check("empty refused",      sdcmd::hexDecode("", 0, out, sizeof out) < 0);
    check("odd length refused", sdcmd::hexDecode("abc", 3, out, sizeof out) < 0);
    check("'g' refused",        sdcmd::hexDecode("0g", 2, out, sizeof out) < 0);
    check("space refused",      sdcmd::hexDecode("0 ", 2, out, sizeof out) < 0);
    check("'-' refused",        sdcmd::hexDecode("-1", 2, out, sizeof out) < 0);
    check("0x prefix refused",  sdcmd::hexDecode("0x12", 4, out, sizeof out) < 0);
    std::string h192(192, 'a'), h194(194, 'a');
    check("96 bytes accepted",  sdcmd::hexDecode(h192.c_str(), 192, out, sizeof out) == 96);
    check("97 bytes refused",   sdcmd::hexDecode(h194.c_str(), 194, out, sizeof out) < 0);

    printf("decimal / crc fields:\n");
    uint32_t x = 7;
    check("0",            sdcmd::parseDec("0", 1, x) && x == 0);
    check("9728",         sdcmd::parseDec("9728", 4, x) && x == 9728);
    check("4294967295",   sdcmd::parseDec("4294967295", 10, x) && x == 4294967295u);
    check("4294967296 overflow refused", !sdcmd::parseDec("4294967296", 10, x));
    check("99999999999 refused",  !sdcmd::parseDec("99999999999", 11, x));
    check("leading zero refused", !sdcmd::parseDec("012", 3, x));
    check("sign refused",         !sdcmd::parseDec("+1", 2, x) && !sdcmd::parseDec("-1", 2, x));
    check("empty refused",        !sdcmd::parseDec("", 0, x));
    check("1e3 refused",          !sdcmd::parseDec("1e3", 3, x));
    check("crc 8 hex",            sdcmd::parseCrc("cbf43926", 8, x) && x == 0xCBF43926u);
    check("crc upper",            sdcmd::parseCrc("CBF43926", 8, x) && x == 0xCBF43926u);
    check("crc 7 hex refused",    !sdcmd::parseCrc("bf43926", 7, x));
    check("crc 9 hex refused",    !sdcmd::parseCrc("0cbf43926", 9, x));
    check("crc non-hex refused",  !sdcmd::parseCrc("cbf4392z", 8, x));

    printf("line framing:\n");
    sdcmd::LineBuf lb;
    auto feedAll = [&](const char* s, size_t n) {
        sdcmd::LineBuf::Kind k = sdcmd::LineBuf::NONE;
        for (size_t i = 0; i < n; i++) { const auto r = lb.feed(s[i]); if (r != sdcmd::LineBuf::NONE) k = r; }
        return k;
    };
    check("LF ends a line", feedAll("SDLS\n", 5) == sdcmd::LineBuf::LINE && !strcmp(lb.buf, "SDLS"));
    check("CRLF: one line", feedAll("END\r\n", 5) == sdcmd::LineBuf::LINE && !strcmp(lb.buf, "END"));
    check("blank lines ignored", feedAll("\r\n\n\r", 4) == sdcmd::LineBuf::NONE);
    const char noise[] = {(char)0xFF, 'S', 'D', 'D', 'E', 'L', ' ', '/', 'W', 'D', '-', '1', '.', 'c', 's', 'v', '\n'};
    check("0xFF in a line: BADBYTE, not dispatched", feedAll(noise, sizeof noise) == sdcmd::LineBuf::BADBYTE);
    const char nul[] = {'R', 'E', 'B', 'O', 'O', 'T', 0, 'x', '\n'};
    check("NUL in a line: BADBYTE", feedAll(nul, sizeof nul) == sdcmd::LineBuf::BADBYTE);
    check("after a bad line the next is clean", feedAll("SDLS\n", 5) == sdcmd::LineBuf::LINE && !strcmp(lb.buf, "SDLS"));
    std::string l200(sdcmd::LINE_CAP, 'a'); l200 += '\n';
    check("200 chars: a line", feedAll(l200.c_str(), l200.size()) == sdcmd::LineBuf::LINE && strlen(lb.buf) == 200);
    std::string l201(sdcmd::LINE_CAP + 1, 'a'); l201 += '\n';
    check("201 chars: OVERLONG, discarded whole", feedAll(l201.c_str(), l201.size()) == sdcmd::LineBuf::OVERLONG);
    std::string l5k(5000, 'b'); l5k += '\n';
    check("5000 chars: OVERLONG", feedAll(l5k.c_str(), l5k.size()) == sdcmd::LineBuf::OVERLONG);
    check("then a clean line", feedAll("END\n", 4) == sdcmd::LineBuf::LINE && !strcmp(lb.buf, "END"));
    check("max D line fits", 2 + 4 + 1 + 2 * sdcmd::CHUNK_MAX <= sdcmd::LINE_CAP);
}

// --- command parsing ------------------------------------------------------
static void testCommands() {
    printf("command matching:\n");
    Rig r;
    r.send("SDLS");            check("SDLS is not mine", r.last == sdcmd::NOT_MINE);
    r.send("SDPUTX /themes/x/theme.json 1 00000000");
    check("SDPUTX is not mine", r.last == sdcmd::NOT_MINE && !r.con.uploading());
    r.send("sdput /themes/x/theme.json 1 00000000");
    check("lowercase sdput is not mine", r.last == sdcmd::NOT_MINE && !r.con.uploading());
    r.send("REBOOTX");         check("REBOOTX is not mine", r.last == sdcmd::NOT_MINE);
    r.send("XREBOOT");         check("XREBOOT is not mine", r.last == sdcmd::NOT_MINE);
    check("REBOOT now: usage, no reboot", r.send("REBOOT now").rfind("ERR", 0) == 0 && r.last == sdcmd::HANDLED);
    check("REBOOT  (trailing space): no reboot", r.send("REBOOT ").rfind("ERR", 0) == 0 && r.last == sdcmd::HANDLED);
    check("REBOOT: reboot", r.send("REBOOT") == "OK rebooting" && r.last == sdcmd::REBOOT);
    check("SDPUT no args",  r.send("SDPUT").rfind("ERR", 0) == 0 && !r.con.uploading());
    check("SDPUT 2 args",   r.send("SDPUT /themes/x/theme.json 10").rfind("ERR", 0) == 0 && !r.con.uploading());
    check("SDPUT extra token", r.send("SDPUT /themes/x/theme.json 10 00000000 x").rfind("ERR", 0) == 0 && !r.con.uploading());
    check("SDPUT double space", r.send("SDPUT  /themes/x/theme.json 10 00000000").rfind("ERR", 0) == 0 && !r.con.uploading());
    check("SDPUT size 1e3", r.send("SDPUT /themes/x/theme.json 1e3 00000000").rfind("ERR", 0) == 0 && !r.con.uploading());
    check("SDPUT size overflow", r.send("SDPUT /themes/x/theme.json 99999999999 00000000").rfind("ERR", 0) == 0);
    check("SDPUT size -1", r.send("SDPUT /themes/x/theme.json -1 00000000").rfind("ERR", 0) == 0);
    check("SDPUT bad crc", r.send("SDPUT /themes/x/theme.json 10 xyz").rfind("ERR", 0) == 0 && !r.con.uploading());
    check("SDPUT /diag.log: path not allowed", r.send("SDPUT /diag.log 10 00000000") == "ERR path not allowed");
    check("SDPUT 300-char path: refused", r.send("SDPUT /" + std::string(150, 'a') + " 10 00000000").rfind("ERR", 0) == 0);
    check("nothing written by any refusal", r.sd.files.empty() && r.sd.dirs.size() == 1);
    check("D with no upload", r.send("D 0 00") == "ERR no upload");
    check("END with no upload", r.send("END") == "ERR no upload");
    check("ABORT with no upload", r.send("ABORT") == "ERR no upload");
    check("SDDEL no arg", r.send("SDDEL").rfind("ERR", 0) == 0);
    check("SDDEL two args", r.send("SDDEL /WD-0001.csv /WD-0002.csv").rfind("ERR", 0) == 0);
    check("refusals left no diag lines", r.sd.notes.empty());
}

// --- the upload -----------------------------------------------------------
static void testUpload() {
    printf("upload:\n");
    {
        Rig r;
        const auto d = bytes(758, 3);
        check("SDPUT reply", r.sdput("/themes/midnight-ember/theme.json", d) ==
                             "OK put /themes/midnight-ember/theme.json 758");
        check("creates /themes and the folder", r.sd.isDir("/themes") && r.sd.isDir("/themes/midnight-ember"));
        check("writes into the temp", r.sd.has("/themes/midnight-ember/theme.json.tmp") &&
                                       !r.sd.has("/themes/midnight-ember/theme.json"));
        for (size_t o = 0; o < d.size(); o += 96) {
            const size_t n = d.size() - o < 96 ? d.size() - o : 96;
            r.chunk((uint32_t)o, d, o, n);
        }
        check("END -> DONE size crc", r.send("END") == doneFor(d));
        check("target has the bytes", r.sd.get("/themes/midnight-ember/theme.json") == d);
        check("temp gone", !r.sd.has("/themes/midnight-ember/theme.json.tmp"));
        check("no .old left", !r.sd.has("/themes/midnight-ember/theme.json.old"));
        check("upload mode over", !r.con.uploading());
        check("one diag line: INFO sd: put ... 758 bytes",
              r.sd.notes.size() == 1 && r.sd.notes[0] == "INFO put /themes/midnight-ember/theme.json 758 bytes");
    }
    {
        Rig r;
        const auto old = bytes(500, 9), d = bytes(9728, 1);
        r.sd.mkdir("/themes"); r.sd.mkdir("/themes/x");
        r.sd.put("/themes/x/emblem.bin", old);
        check("replace an existing 9728-byte emblem", r.upload("/themes/x/emblem.bin", d) == doneFor(d));
        check("replaced", r.sd.get("/themes/x/emblem.bin") == d && !r.sd.has("/themes/x/emblem.bin.old"));
    }
    {
        Rig r;   // an existing folder in another case is the same folder on FAT
        r.sd.mkdir("/themes"); r.sd.mkdir("/themes/Ember");
        const auto d = bytes(10, 2);
        check("folder case differs: still works", r.upload("/themes/ember/THEME.JSON", d) == doneFor(d) &&
                                                  r.sd.get("/themes/Ember/theme.json") == d);
    }
    {
        Rig r;
        const auto old = bytes(300, 5), d = bytes(300, 6);
        r.sd.mkdir("/themes"); r.sd.mkdir("/themes/x");
        r.sd.put("/themes/x/theme.json", old);
        char l[96];
        snprintf(l, sizeof l, "SDPUT /themes/x/theme.json 300 %08x", (unsigned)(sdcmd::crc32(0, d.data(), 300) ^ 1));
        r.send(l);
        for (size_t o = 0; o < 300; o += 96) r.chunk((uint32_t)o, d, o, 300 - o < 96 ? 300 - o : 96);
        const std::string e = r.send("END");
        check("CRC mismatch: ERR", e.rfind("ERR crc", 0) == 0);
        check("CRC mismatch: target untouched", r.sd.get("/themes/x/theme.json") == old);
        check("CRC mismatch: temp removed", !r.sd.has("/themes/x/theme.json.tmp"));
        check("CRC mismatch: upload over", !r.con.uploading());
        check("CRC mismatch: a WARN diag line", r.sd.notes.size() == 1 && r.sd.notes[0].rfind("WARN", 0) == 0);
    }
    {
        Rig r;
        const auto d = bytes(200, 7);
        r.sdput("/themes/x/theme.json", d);
        r.chunk(0, d, 0, 96);
        check("END early: ERR short", r.send("END").rfind("ERR short", 0) == 0);
        check("END early: temp removed, no target", !r.sd.has("/themes/x/theme.json.tmp") && !r.sd.has("/themes/x/theme.json"));
    }
    {
        Rig r;
        const auto d = bytes(300, 8);
        r.sdput("/themes/x/theme.json", d);
        check("chunk 0", r.chunk(0, d, 0, 96) == "OK 0");
        check("duplicate chunk 0 (same bytes): OK", r.chunk(0, d, 0, 96) == "OK 0");
        check("duplicate of part of chunk 0: OK", r.chunk(10, d, 10, 20) == "OK 10");
        auto bad = d; bad[5] ^= 0x40;
        check("repeat with different bytes: ERR", r.chunk(0, bad, 0, 96).rfind("ERR", 0) == 0);
        check("gap (192 while at 96): ERR gap", r.chunk(192, d, 192, 96).rfind("ERR gap", 0) == 0);
        check("upload survives a gap", r.con.uploading());
        check("straddling chunk (64..160)", r.chunk(64, d, 64, 96) == "OK 64");
        check("next chunk at 160", r.chunk(160, d, 160, 96) == "OK 160");
        check("last (short) chunk 256..300", r.chunk(256, d, 256, 44) == "OK 256");
        check("END after recovery", r.send("END") == doneFor(d) && r.sd.get("/themes/x/theme.json") == d);
    }
    {
        Rig r;
        const auto d = bytes(100, 1);
        r.sdput("/themes/x/theme.json", d);
        r.chunk(0, d, 0, 96);
        check("chunk beyond declared size: ERR", r.send("D 96 " + hexOf(d.data(), 8)).rfind("ERR", 0) == 0);
        check("bad hex: ERR", r.send("D 96 zz").rfind("ERR", 0) == 0);
        check("odd hex: ERR", r.send("D 96 abc").rfind("ERR", 0) == 0);
        check("97-byte chunk: ERR", r.send("D 96 " + std::string(194, 'a')).rfind("ERR", 0) == 0);
        check("offset overflow: ERR", r.send("D 4294967296 00").rfind("ERR", 0) == 0);
        check("D missing data: ERR", r.send("D 96").rfind("ERR", 0) == 0);
        check("D extra token: ERR", r.send("D 96 00 00").rfind("ERR", 0) == 0);
        check("only 96 bytes landed", r.sd.get("/themes/x/theme.json.tmp").size() == 96);
        check("other commands refused while uploading",
              r.send("SDLS").rfind("ERR upload in progress", 0) == 0 && r.last == sdcmd::HANDLED);
        check("SDDEL refused while uploading", r.send("SDDEL /WD-0001.csv").rfind("ERR", 0) == 0);
        check("finish", r.chunk(96, d, 96, 4) == "OK 96" && r.send("END") == doneFor(d));
    }
    {
        Rig r;
        const auto old = bytes(50, 2), d = bytes(300, 3);
        r.sd.mkdir("/themes"); r.sd.mkdir("/themes/x");
        r.sd.put("/themes/x/theme.json", old);
        r.sdput("/themes/x/theme.json", d);
        r.chunk(0, d, 0, 96);
        r.now += sdcmd::IDLE_MS - 1;
        char rep[sdcmd::REPLY_CAP] = "";
        check("9.999 s idle: still running", !r.con.tick(r.now, rep, sizeof rep) && r.con.uploading());
        r.chunk(96, d, 96, 96);                    // resets the clock
        r.now += sdcmd::IDLE_MS - 1;
        check("a chunk resets the idle clock", !r.con.tick(r.now, rep, sizeof rep) && r.con.uploading());
        r.now += 1;
        check("10 s idle: aborts with a line", r.con.tick(r.now, rep, sizeof rep) && !strncmp(rep, "ERR", 3));
        check("timeout: upload over", !r.con.uploading());
        check("timeout: temp removed, target untouched",
              !r.sd.has("/themes/x/theme.json.tmp") && r.sd.get("/themes/x/theme.json") == old);
        check("timeout: chunk now refused", r.chunk(192, d, 192, 96) == "ERR no upload");
        r.now = 0xFFFFFF00u;                        // across the millis() wrap
        r.sdput("/themes/x/theme.json", d);
        r.now += 0x200;
        check("wrap: not yet idle", !r.con.tick(r.now, rep, sizeof rep));
        r.now += sdcmd::IDLE_MS;
        check("wrap: idle aborts", r.con.tick(r.now, rep, sizeof rep) && !r.con.uploading());
    }
    {
        Rig r;
        const auto a = bytes(300, 1), b = bytes(120, 2);
        r.sdput("/themes/x/theme.json", a);
        r.chunk(0, a, 0, 96);
        check("new SDPUT aborts the old one", r.sdput("/themes/y/emblem.bin", b) == "OK put /themes/y/emblem.bin 120");
        check("old temp removed", !r.sd.has("/themes/x/theme.json.tmp"));
        check("an abort WARN was logged", !r.sd.notes.empty() && r.sd.notes[0].rfind("WARN", 0) == 0);
        check("old upload's chunks now refused", r.chunk(96, a, 96, 96).rfind("ERR", 0) == 0);
        check("new upload completes", r.chunk(0, b, 0, 96) == "OK 0" && r.chunk(96, b, 96, 24) == "OK 96" &&
                                      r.send("END") == doneFor(b));
        check("old target never created", !r.sd.has("/themes/x/theme.json"));
        r.sdput("/themes/x/theme.json", a);
        check("ABORT: OK aborted", r.send("ABORT") == "OK aborted" && !r.con.uploading() &&
                                   !r.sd.has("/themes/x/theme.json.tmp"));
        r.sdput("/themes/x/theme.json", a);
        check("a refused SDPUT also ends the running one",
              r.send("SDPUT /diag.log 10 00000000") == "ERR path not allowed" && !r.con.uploading() &&
              !r.sd.has("/themes/x/theme.json.tmp"));
        r.sdput("/themes/x/theme.json", a);
        check("REBOOT during an upload aborts it", r.send("REBOOT") == "OK rebooting" && !r.con.uploading() &&
                                                    !r.sd.has("/themes/x/theme.json.tmp"));
    }
    {
        Rig r;
        const auto old = bytes(40, 4), d = bytes(300, 5);
        r.sd.mkdir("/themes"); r.sd.mkdir("/themes/x");
        r.sd.put("/themes/x/theme.json", old);
        r.sd.failWriteAfter = 1;
        r.sdput("/themes/x/theme.json", d);
        check("first chunk ok", r.chunk(0, d, 0, 96) == "OK 0");
        check("write failure: ERR", r.chunk(96, d, 96, 96).rfind("ERR write", 0) == 0);
        check("write failure: upload over, temp removed",
              !r.con.uploading() && !r.sd.has("/themes/x/theme.json.tmp"));
        check("write failure: target untouched", r.sd.get("/themes/x/theme.json") == old);
    }
    {
        Rig r;
        const auto d = bytes(3000, 5);
        r.sd.capacity = 1000;                      // a full card
        r.sdput("/themes/x/theme.json", d);
        std::string e;
        for (size_t o = 0; o < d.size(); o += 96) {
            e = r.chunk((uint32_t)o, d, o, d.size() - o < 96 ? d.size() - o : 96);
            if (e.rfind("ERR", 0) == 0) break;
        }
        check("card full: ERR write", e.rfind("ERR write", 0) == 0 && !r.con.uploading());
        check("card full: temp removed", !r.sd.has("/themes/x/theme.json.tmp"));
    }
    {
        Rig r;
        r.sd.card = false;
        check("no card: ERR", r.sdput("/themes/x/theme.json", bytes(10, 1)).rfind("ERR no card", 0) == 0 &&
                              !r.con.uploading());
    }
    {
        Rig r;
        r.sd.failMkdir = true;
        check("mkdir fails: ERR", r.sdput("/themes/x/theme.json", bytes(10, 1)).rfind("ERR", 0) == 0 &&
                                  !r.con.uploading());
    }
    {
        Rig r;
        r.sd.put("/themes", bytes(3, 1));        // a FILE named themes
        check("/themes is a file: ERR", r.sdput("/themes/x/theme.json", bytes(10, 1)).rfind("ERR", 0) == 0);
        check("...and the file is untouched", r.sd.get("/themes") == bytes(3, 1));
    }
    {
        Rig r;
        r.sd.mkdir("/themes"); r.sd.mkdir("/themes/x");
        r.sd.put("/themes/x/theme.json.tmp", bytes(77, 7));   // a stale temp
        const auto d = bytes(20, 1);
        check("a stale temp is replaced", r.upload("/themes/x/theme.json", d) == doneFor(d));
    }
    {
        Rig r;
        const auto old = bytes(40, 4), d = bytes(100, 5);
        r.sd.mkdir("/themes"); r.sd.mkdir("/themes/x");
        r.sd.put("/themes/x/theme.json", old);
        r.sdput("/themes/x/theme.json", d);
        r.chunk(0, d, 0, 96); r.chunk(96, d, 96, 4);
        r.sd.failRename = true;
        check("rename fails: ERR", r.send("END").rfind("ERR", 0) == 0);
        check("rename fails: target untouched, temp removed",
              r.sd.get("/themes/x/theme.json") == old && !r.sd.has("/themes/x/theme.json.tmp") &&
              !r.sd.has("/themes/x/theme.json.old"));
    }
    {
        Rig r;
        const auto d = bytes(100, 5);
        r.sdput("/themes/x/theme.json", d);
        r.chunk(0, d, 0, 96); r.chunk(96, d, 96, 4);
        r.sd.failClose = true;
        check("close (flush) fails: ERR, nothing installed", r.send("END").rfind("ERR", 0) == 0 &&
              !r.sd.has("/themes/x/theme.json") && !r.sd.has("/themes/x/theme.json.tmp"));
    }
    {
        Rig r;
        const auto d = bytes(100, 5);
        r.sdput("/themes/x/theme.json", d);
        r.chunk(0, d, 0, 96);
        r.sd.card = false;                        // pulled mid-upload
        check("card pulled: ERR", r.chunk(96, d, 96, 4).rfind("ERR", 0) == 0 && !r.con.uploading());
    }
    {
        Rig r;
        check("size over cap: ERR too large",
              r.send("SDPUT /themes/x/emblem.bin 9729 00000000").rfind("ERR too large", 0) == 0 &&
              !r.con.uploading() && r.sd.dirs.size() == 1);
    }
}

static void testDelete() {
    printf("delete / rmdir:\n");
    Rig r;
    r.active = "/WD-20260119-120000.csv";
    r.sd.mkdir("/themes"); r.sd.mkdir("/themes/x");
    r.sd.put("/themes/x/theme.json", bytes(5, 1));
    r.sd.put("/themes/x/emblem.bin", bytes(5, 1));
    r.sd.put("/WD-0001.CSV", bytes(5, 1));
    r.sd.put("/WD-20260119-120000.csv", bytes(5, 1));
    r.sd.put("/diag.log", bytes(5, 1));
    check("del the live session: ERR", r.send("SDDEL /WD-20260119-120000.csv").rfind("ERR", 0) == 0 &&
                                       r.sd.has("/WD-20260119-120000.csv"));
    check("del diag.log: ERR path not allowed", r.send("SDDEL /diag.log") == "ERR path not allowed" &&
                                                r.sd.has("/diag.log"));
    check("del an old session (.CSV): OK", r.send("SDDEL /WD-0001.csv") == "OK" && !r.sd.has("/WD-0001.CSV"));
    check("diag line for the delete", !r.sd.notes.empty() && r.sd.notes.back() == "INFO del /WD-0001.csv");
    check("del it again: ERR no such file", r.send("SDDEL /WD-0001.csv").rfind("ERR no such", 0) == 0);
    check("rmdir a non-empty folder: ERR", r.send("SDRMDIR x").rfind("ERR", 0) == 0 && r.sd.isDir("/themes/x"));
    check("del theme.json: OK", r.send("SDDEL /themes/x/theme.json") == "OK");
    check("del emblem.bin: OK", r.send("SDDEL /themes/x/EMBLEM.BIN") == "OK");
    check("rmdir the empty folder: OK", r.send("SDRMDIR x") == "OK" && !r.sd.isDir("/themes/x"));
    check("rmdir it again: ERR", r.send("SDRMDIR x").rfind("ERR", 0) == 0);
    check("rmdir ..: ERR path not allowed", r.send("SDRMDIR ..") == "ERR path not allowed");
    check("rmdir with two args: ERR", r.send("SDRMDIR a b").rfind("ERR", 0) == 0);
    r.sd.card = false;
    check("no card: ERR", r.send("SDDEL /WD-0002.csv").rfind("ERR no card", 0) == 0);
}

// --- tools/sdput.py's own transcript, replayed ---------------------------
static bool readFile(const char* path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    uint8_t b[4096];
    size_t k;
    out.clear();
    while ((k = fread(b, 1, sizeof b, f)) > 0) out.insert(out.end(), b, b + k);
    fclose(f);
    return true;
}

static void testGolden() {
    printf("sdput.py transcript (sim/sdput_golden.txt):\n");
    FILE* f = fopen("sdput_golden.txt", "r");
    check("fixture present", f != nullptr);
    if (!f) return;
    Rig r;
    char line[512];
    int lines = 0, oks = 0, dones = 0;
    bool allOk = true;
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (!n || line[0] == '#') continue;
        // "> line" is sent; "< reply" is what the tool expects back.
        if (line[0] == '>' && line[1] == ' ') { r.send(line + 2); lines++; continue; }
        if (line[0] == '<' && line[1] == ' ') {
            const bool ok = std::string(r.reply) == std::string(line + 2);
            if (!ok) { printf("    expected '%s' got '%s'\n", line + 2, r.reply); allOk = false; }
            oks += ok;
            if (!strncmp(line + 2, "DONE", 4)) dones++;
        }
    }
    fclose(f);
    char what[96];
    snprintf(what, sizeof what, "%d lines replayed, every reply as the tool expects", lines);
    check(what, allOk && lines > 10 && oks > 10);
    check("two files completed (emblem.bin then theme.json)", dones == 2);
    std::vector<uint8_t> j, e;
    check("theme.json byte for byte", readFile("packs/cosmic-clone/theme.json", j) &&
                                      r.sd.get("/themes/cosmic-clone/theme.json") == j);
    check("emblem.bin byte for byte", readFile("packs/cosmic-clone/emblem.bin", e) &&
                                      r.sd.get("/themes/cosmic-clone/emblem.bin") == e);
}

int main() {
    testPutPolicy();
    testDelPolicy();
    testDecoders();
    testCommands();
    testUpload();
    testDelete();
    testGolden();
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
