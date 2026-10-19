// Host test for the logging state machine. sdlog is faked: it records calls
// instead of touching a card, so start/stop/timer/resume are pinned without
// hardware.
#include "../device/logging.h"
#include "../device/sdlog.h"
#include <stdio.h>
#include <string.h>

// --- fake sdlog -----------------------------------------------------------
namespace { bool cardPresent = true; int writes = 0; char lastName[32];
           uint32_t maxOnCard = 0; }
namespace sdlog {
bool begin() { return cardPresent; }
bool present() { return cardPresent; }
bool writeAtomic(const char* name, uint32_t (*)(class Print&, uint32_t&),
                 uint32_t& rows, uint32_t& skipped) {
    if (!cardPresent) return false;
    writes++; strncpy(lastName, name, sizeof lastName - 1);
    rows = 7; skipped = 1; return true;
}
uint32_t maxIndex(const char*, const char*) { return maxOnCard; }
}

static uint32_t fakeBody(class Print&, uint32_t&) { return 0; }

static int fails = 0;
static void check(const char* what, bool ok) {
    printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
    if (!ok) fails++;
}

int main() {
    using namespace logging;

    printf("start opens a session and arms the timer:\n");
    cardPresent = true; writes = 0;
    begin(false, fakeBody, 0, 1792972800u);
    check("begins idle", state() == IDLE);
    start(1000, 1792972800u);
    check("now active", state() == ACTIVE);
    check("start wrote once (opened the file)", writes == 1);

    printf("the timer flushes at FLUSH_MS and not before:\n");
    writes = 0;
    tick(1000 + FLUSH_MS - 1, 1792972800u);
    check("no flush before the interval", writes == 0);
    tick(1000 + FLUSH_MS, 1792972800u);
    check("one flush at the interval", writes == 1);

    printf("stop forces exactly one final flush and closes:\n");
    writes = 0;
    stop(1000 + FLUSH_MS + 5);
    check("stop flushed once", writes == 1);
    check("now idle", state() == IDLE);
    check("file name cleared", fileName()[0] == 0);

    printf("toggle alternates:\n");
    begin(false, fakeBody, 0, 1792972800u);
    toggle(10, 1792972800u); check("toggle -> active", state() == ACTIVE);
    toggle(20, 0);           check("toggle -> idle", state() == IDLE);

    printf("auto-resume:\n");
    begin(true, fakeBody, 0, 1792972800u);
    check("begin(wasActive) resumes active", state() == ACTIVE);
    begin(false, fakeBody, 0, 1792972800u);
    check("begin(idle) stays idle", state() == IDLE);

    printf("NO_CARD is entered and left:\n");
    cardPresent = false;
    begin(false, fakeBody, 0, 1792972800u);
    start(0, 1792972800u);
    check("start with no card -> NO_CARD", state() == NO_CARD);
    check("wantsActive is true despite no card", wantsActive());
    cardPresent = true;
    tick(100, 1792972800u);
    check("card appears -> ACTIVE", state() == ACTIVE);

    printf("resume uses a new file each time:\n");
    begin(true, fakeBody, 0, 1792972800u);
    char first[32] = {0}; strncpy(first, fileName(), sizeof first - 1);
    stop(50);
    begin(true, fakeBody, 60, 1792972800u + 60);
    check("a later session names a different file", strcmp(first, fileName()) != 0);

    printf("dateless counter advances past files already on the card:\n");
    // No date (epoch 0) -> the WD-NNNN counter path. It must scan the card and
    // start past the highest existing WD file, so a reboot before a GPS fix
    // cannot overwrite an earlier dateless session (spec s7).
    cardPresent = true; maxOnCard = 7;
    begin(false, fakeBody, 0, 0);
    start(0, 0);
    check("names past WD-0007 -> WD-0008", strcmp(fileName(), "/WD-0008.CSV") == 0);

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
