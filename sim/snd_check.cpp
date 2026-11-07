// Prints what the device's alert.snd parser (themepack::parseSound) makes of a file: "OK n0 n1 n2" or "ERR <reason>".
// The designer's tests run it to hold the browser's alert.snd writer to the board's parser byte for byte.
// Build: make -C sim snd_check      Use: sim/snd_check FILE
#include "../ui/themepack.h"
#include <cstdio>
#include <cstring>
#include <vector>
int main(int argc, char** argv) {
    if (argc != 2) { fprintf(stderr, "usage: snd_check FILE\n"); return 2; }
    FILE* f = fopen(argv[1], "rb"); if (!f) { puts("ERR cannot open"); return 2; }
    std::vector<uint8_t> v; uint8_t b[4096]; size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) v.insert(v.end(), b, b + n);
    fclose(f);
    themepack::Sound s; char why[64];
    // an int16_t view needs an aligned buffer: copy into one
    std::vector<int16_t> al((v.size() + 1) / 2 + 1); if (!v.empty()) memcpy(al.data(), v.data(), v.size());
    if (themepack::parseSound((const uint8_t*)al.data(), v.size(), s, why, sizeof why)) printf("OK %u %u %u\n", s.n[0], s.n[1], s.n[2]);
    else printf("ERR %s\n", why);
    return 0;
}
