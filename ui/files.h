// FILES -- the session logs on the SD card: see them, and delete one.
//
// Everything a finger can do here is pure UI state on ui::Nav, decided in
// this file and host-tested (sim/test_files.cpp): row selection, scrolling,
// the DELETE button, the YES / NO confirmation and its timeouts, and the
// protection of the file being logged to. The card itself is the caller's:
// nav only raises filesScanRequested / fileDeleteRequested, and the device
// re-checks the delete policy on the file's name (device/filescan.h, which
// is the serial SDDEL's own policy from device/sdcmd.h) before it removes
// anything.
//
// The confirmation:
//   - opens only from DELETE with a row selected that is not the session
//     being logged to (that row shows REC and DELETE is drawn disabled);
//   - NO is the default: the larger target, and a tap anywhere but YES
//     (or the back control) cancels;
//   - every tap in the first CONFIRM_ARM_MS after it appears is ignored, so a
//     bounce of the DELETE tap can neither confirm nor cancel it;
//   - cancels itself after CONFIRM_MS, on leaving the screen, on any rescan
//     of the list, and if the selected file becomes the logging session.
#pragma once
#include <stdint.h>
#include "model.h"
#include "nav.h"

namespace ui {

constexpr uint32_t CONFIRM_MS     = 10000;  // the confirmation cancels itself
constexpr uint32_t CONFIRM_ARM_MS = 400;    // ...and ignores taps this soon
constexpr uint32_t RESULT_MS      = 2000;   // "DELETED" / "FAILED" stays up

enum FileResult : uint8_t { FR_NONE = 0, FR_DELETED = 1, FR_FAILED = 2 };

// The clock and the timeouts. Call once per poll, before navTouch.
void filesTick(Nav& n, uint32_t nowMs);

// Open the screen (resets selection, scroll and confirm; asks for a scan),
// and leave it (back to ATTUNE page 2, everything cancelled).
void filesEnter(Nav& n);
void filesLeave(Nav& n);

// A tap on the FILES screen other than the back control. Returns true when
// the whole screen must be repainted (never, today: the body's own dirty
// tracking covers every change here).
bool filesTap(Nav& n, int x, int y);

// The deepest the list can scroll: the caller sets nav.maxScroll from it.
int16_t filesMaxScroll(const Nav& n);

// The caller rebuilt its list. `prevSel` is the name the selection was on
// before (nullptr or "" for none): the selection follows that name, so a
// failed delete keeps it and a deleted file loses it. Closes the confirm and
// clamps the scroll.
void filesRescanned(Nav& n, const FileList& L, const char* prevSel);

// The list index of the logging session (-1 none). A confirmation open on
// that row is cancelled: the file is in use now.
void filesSetActive(Nav& n, int idx);

// The device's verdict on a delete it was asked for.
void filesResult(Nav& n, bool ok, uint32_t nowMs);

// The row's text: "2026-01-27 15:30" from WD-20260127-153012.csv, else the
// name without ".csv" ("WD-0012"). Always NUL-terminates when cap > 0.
void fileLabel(const char* name, char* out, int cap);
// The confirm card's line: the same, to the second ("2026-01-27 15:30:12"),
// so two logs started in the same minute can be told apart.
void fileLabelLong(const char* name, char* out, int cap);
// The header's "28G CARD": the card's capacity, "" with no card or an
// unknown size. And the count line's right end: the logs' total ("3.1G",
// "3.1G+" when the walk was bounded, so it is a floor), "" with none.
// out holds >= 16.
void filesCardText(const State& s, char* out);
void filesLogsText(const State& s, char* out);
// The count line: "5 LOGS", "1 LOG", "64/80 LOGS", or "LIST INCOMPLETE" when
// the walk stopped at its bound (the newest logs may be the missing ones).
void filesCountText(const State& s, char* out);
// "512B", "1.2K", "34K", "980K", "1.2M", "12M", "1.9G", "28G", "1.1T":
// at most 4 characters plus NUL. kb is a size in KiB (the card), b in bytes.
void fmtBytes(uint32_t b, char* out);
void fmtKB(uint32_t kb, char* out);

// Index of `name` in the list (ASCII case-insensitive), -1 if absent.
int fileIndexOf(const FileList& L, const char* name);

// Fill State's FILES fields from the list and the screen state.
void filesPublish(State& s, const Nav& n, const FileList& L);

} // namespace ui
