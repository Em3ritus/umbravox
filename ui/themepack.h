// Theme pack: the parsed form of a /themes/<folder>/theme.json on the SD card.
//
// A pack is untrusted data. parse() is bounded, allocation-free and never
// assumes the input is NUL terminated; on any defect it returns false with a
// short reason and leaves nothing half-trusted for the caller (check the
// return value, not the struct).
//
// Deliberately free of palette.h / Arduino so it links into the host sim, the
// host tests and the device alike.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace themepack {

constexpr int FORMAT = 1;
constexpr int MAX_PACKS = 8;
constexpr int MAX_NAME = 20;
constexpr int MAX_HEADER = 18;
constexpr int MAX_AUTHOR = 24;
constexpr size_t MAX_JSON = 4096;
constexpr size_t MAX_EMBLEM = 9728;
constexpr size_t MAX_PACK = 65536;
constexpr int MAX_EMBLEM_DIM = 96;
constexpr int MAX_EMBLEM_COLORS = 16;
constexpr int N_COLORS = 20;

enum Dial { DIAL_COMPACT, DIAL_LARGE };
enum Centre { CENTRE_HEPTAGRAM, CENTRE_EMBLEM };
enum Rite { RITE_WARD, RITE_SUPERNOVA, RITE_VORTEX, RITE_COMET, RITE_LOCKON };

enum Role : uint8_t {
    R_VOID, R_PANEL, R_GRID, R_EDGE, R_EDGELIT, R_DIM, R_TEXT, R_BRIGHT,
    R_WITCH, R_WITCHHI, R_BONE, R_SWEEP, R_SWEEPHI, R_LIVE, R_LIVEHI,
    R_MAGENTA, R_ICE, R_GREEN, R_AMBER, R_RED
};

struct Pack {
    char name[MAX_NAME + 1];
    char author[MAX_AUTHOR + 1];
    char header[MAX_HEADER + 1];
    Dial dial;
    Centre centre;
    Rite rite;
    uint16_t sweeps;            // bitmask over ui::Sweep
    uint8_t sweep;              // ui::Sweep; always a member of `sweeps`
    uint16_t color[N_COLORS];   // RGB565, indexed by Role
};

// Parse n bytes of theme.json. On failure returns false and writes a short
// reason into why (if whyCap > 0). `out` is only meaningful on true.
bool parse(const char* json, size_t n, Pack& out, char* why, size_t whyCap);

}  // namespace themepack
