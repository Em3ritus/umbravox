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

// A parsed emblem.bin: indexed-colour art for the centrepiece. `px` points INTO
// the buffer given to parseEmblem (w*h palette indices, row-major), so that
// buffer must outlive the Emblem. Only meaningful after parseEmblem returns true.
struct Emblem {
    uint8_t w, h, ncol;
    uint16_t pal[MAX_EMBLEM_COLORS];  // RGB565
    const uint8_t* px;
};

// Parse n bytes of emblem.bin: 'U','V','E',1, w, h, ncol, 0, ncol little-endian
// RGB565 values, then w*h indices. Everything is validated (size, limits, total
// length, every index) before `out.px` is set; on failure returns false with a
// short reason in why (if whyCap > 0) and `out.px` is null.
bool parseEmblem(const uint8_t* data, size_t n, Emblem& out, char* why, size_t whyCap);

// Status roles (amber, red) are replaced outright with #F59E0B / #EF4444 when
// their contrast against void or panel is under ui::MIN_EDGE_CONTRAST_X10.
// Returns true if anything changed.
bool sanitizeStatus(Pack& p);

// ---- Admission: which pack folders are loaded, and whether to skip one ------
// Pure logic (no SD access) so the loader's decisions are host-testable.

// Folder name is [A-Za-z0-9_-]{1,31}. Anything else (".", "..", separators,
// spaces, non-ASCII, NULL) is not a pack folder.
bool folderNameOk(const char* folder);

enum Admit { ADMIT_OK, ADMIT_BAD_FOLDER, ADMIT_RESERVED, ADMIT_DUPLICATE, ADMIT_FULL };

// Decide whether a parsed pack in `folder` joins the n already admitted packs
// (takenNames[0..nTaken), NULL entries ignored). The name must not be a
// built-in's and must be unique (exact, case-sensitive); at most MAX_PACKS.
Admit admit(const char* folder, const Pack& p, const char* const* takenNames, int nTaken);

// Short human-readable reason for the diagnostic log; never empty, no newline.
const char* admitWhy(Admit a);

// Sort folder names byte-wise ascending (stable), so the same MAX_PACKS always
// win. Each entry is read as at most 32 bytes; a missing NUL ends it there.
void sortNames(char (*names)[32], int n);

// Collect one directory entry into a bounded listing of `cap` names: appended
// while there is room; once full it replaces the byte-wise largest name held
// if `name` is smaller, so the cap smallest names survive whatever order the
// directory yields them in (sort the result with sortNames). Returns the new
// count. NULL, or a name of 32 bytes or more (it would not fit), is not kept.
int keepName(char (*names)[32], int n, int cap, const char* name);

// Boot-loop guard, decision half. The loader persists formatLoading()'s record
// before loading a pack and clears it after; if the record survives a reboot,
// skipAfterCrash() is true only for that exact folder at that exact total size
// (so an edited pack gets another go). Anything else skips nothing.
void formatLoading(char* out, size_t cap, const char* folder, uint32_t totalSize);
bool skipAfterCrash(const char* record, const char* folder, uint32_t totalSize);

// Boot-loop guard, memory half: the packs that have crashed the loader.
//
// One in-flight record is not enough: with two crashing packs A < B, each
// boot's crash overwrites the other's record and the board alternates
// between them forever. So the in-flight record ("loading") is folded at boot
// into a persistent list ("badpacks") of every folder:size that has crashed,
// and any folder matching an entry is skipped. Entries are pruned when their
// folder is no longer listed or its size changed (an edited pack is tried
// again). The list is stored as records joined by ','.
//
// BAD_MAX equals the loader's folder limit: after pruning, every entry names
// a distinct folder that is listed this boot, so the list can never need
// more than one slot per examined folder.
constexpr int    BAD_MAX = 32;
constexpr size_t BAD_REC = 48;                        // one formatLoading record
constexpr size_t BAD_LIST_CAP = BAD_MAX * 44 + 1;     // 43 + ',' each, plus NUL

struct BadList {
    char rec[BAD_MAX][BAD_REC];
    int  n;
};

// Parse up to n bytes of a stored list (need not be NUL terminated). Only
// well-formed records ("<folderNameOk>:<1-10 digits>") are kept, duplicates
// once, at most BAD_MAX; anything else is dropped. Returns out.n.
int badParse(const char* s, size_t n, BadList& out);

// Join the list into out as "r1,r2,...". Stops before a record that would not
// fit, so the result is always whole records and NUL terminated (cap > 0).
// Returns the length written.
size_t badFormat(const BadList& l, char* out, size_t cap);

// Add a well-formed record unless it is already held. False (list unchanged)
// if it is malformed, a duplicate, or the list is full.
bool badAdd(BadList& l, const char* rec);

// Keep only entries that match (skipAfterCrash) a listed folder at its size.
void badPrune(BadList& l, const char (*names)[32], const uint32_t* sizes, int nNames);

// Whether this folder at this size has crashed the loader before.
bool badMatch(const BadList& l, const char* folder, uint32_t size);

// The whole boot-time decision: parse the stored list (storedLen bytes), fold
// in the in-flight `loading` record (NULL or "" if none), prune against this
// boot's folders, and format the result into newStored. Returns true when
// newStored differs from what is stored, i.e. the list must be rewritten
// (an empty newStored means erase it). `out` is the list to skip against.
bool badBoot(const char* stored, size_t storedLen, const char* loading,
             const char (*names)[32], const uint32_t* sizes, int nNames,
             BadList& out, char* newStored, size_t cap);

}  // namespace themepack
