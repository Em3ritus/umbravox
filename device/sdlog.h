// The SD card: mount, append to files, and diagnostics. Knows nothing of
// WiGLE. logging owns the session name and the bookkeeping.
#pragma once
#include <stdint.h>
class Print;

namespace sdlog {

bool begin();     // mount on the shared SPI bus; false and stays false if none
bool present();   // a card is mounted

// The WiGLE session file is APPENDED to, one batch per flush (logging.cpp /
// applog.h own the bookkeeping): openAppend, writeAppend..., closeAppend. One
// append is open at a time. closeAppend flushes (fsync) and returns false if
// any write since openAppend failed or came up short. The whole-file rewrite
// (writeAtomic) is gone: its cost grew with the file and froze the UI.
// `name` must be an absolute path (a leading '/'), as SD.open requires.
bool exists(const char* name);
bool openAppend(const char* name, uint32_t& size);   // created if missing
bool writeAppend(const char* data, uint32_t len);
bool closeAppend();
// Cut `name` back to `len` bytes: removes the residue of a failed append.
bool truncateTo(const char* name, uint32_t len);

// Bring-up diagnostics (serial console). list() prints each root file as
// "<size>  <name>"; dump() streams one file's bytes to `out` and returns
// false if the card is absent or the file does not exist.
void list(Print& out);
bool dump(Print& out, const char* name);

// Highest N among root files named "<prefix>NNNN<suffix>", or 0 if none.
// `logging` uses it to advance the dateless session counter past files
// already on the card, so a reboot before a GPS fix can't overwrite one.
uint32_t maxIndex(const char* prefix, const char* suffix);

// Case-insensitive on the prefix and the suffix. Does `name` (a bare or '/'-led basename) look like "<prefix>NNNN<suffix>"?
// Sets `v` to N. Pure, so the host test runs the very matcher the card scan uses.
inline char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
inline bool matchIndex(const char* name, const char* prefix, const char* suffix, uint32_t& v) {
    if (name[0] == '/') name++;
    if (prefix[0] == '/') prefix++;
    // ASCII case-insensitive, like FAT: files from older firmware end ".CSV".
    for (; *prefix; prefix++, name++) if (lower(*prefix) != lower(*name)) return false;
    uint32_t n = 0; bool any = false;
    while (*name >= '0' && *name <= '9') { n = n * 10 + (uint32_t)(*name - '0'); name++; any = true; }
    for (; *suffix; suffix++, name++) if (lower(*suffix) != lower(*name)) return false;
    if (!any || *name) return false;
    v = n;
    return true;
}

// Append-mode helpers for the diagnostic log (elog). These grow a file in
// place; the caller (elog) owns rotation so it never fills the
// card. All are best-effort and no-ops when no card is mounted.
uint32_t sizeOf(const char* name);                                  // bytes, or 0
bool     append(const char* name, const uint8_t* data, uint32_t len);
bool     rotate(const char* name, const char* to);                  // to <- name

// The longest a write held up the loop (ms) since this was last called, across
// the session appends (openAppend..closeAppend) and append. Reading it clears it; perflog logs it so the cost of
// the SD card itself is visible next to the frame times it competes with.
uint32_t takeMaxStallMs();

} // namespace sdlog
