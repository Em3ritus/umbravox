// The wardrive logging session: start/stop/toggle, the flush timer, and
// auto-resume after a reboot. Pure -- no Arduino, no SD -- so it tests on the
// host against a fake sdlog. It reaches the card only through sdlog.h.
#pragma once
#include <stdint.h>

// Declared at global scope on purpose: `class Print&` written inside the
// namespace would declare logging::Print, a different type from Arduino's (and
// sdlog's) ::Print, and Body would not match sdlog::writeAtomic.
class Print;

namespace logging {

enum State : uint8_t { IDLE, ACTIVE, NO_CARD };

// The document writer (main injects writeWigle). Kept as a pointer so logging
// carries no dependency on the exporters or Print.
typedef uint32_t (*Body)(Print&, uint32_t&);

constexpr uint32_t FLUSH_MS = 30000;

// At boot: resume if we died logging. `body` is the writer; `nowEpoch` seeds
// the session filename when a date is already known.
void begin(bool wasActive, Body body, uint32_t nowMs, uint32_t nowEpoch);

void toggle(uint32_t nowMs, uint32_t nowEpoch);
void start(uint32_t nowMs, uint32_t nowEpoch);
void stop(uint32_t nowMs);

// Every loop. Flushes at FLUSH_MS while ACTIVE; promotes NO_CARD to ACTIVE
// when a card appears.
void tick(uint32_t nowMs, uint32_t nowEpoch);

State       state();
bool        wantsActive();   // the flag to persist: true from start to stop
uint32_t    sessionSecs(uint32_t nowMs);
const char* fileName();      // current session file, "" when idle
uint32_t    lastRows();
uint32_t    lastSkipped();
// Latched true when a flush failed with the card still present (a card-full or
// I/O error, not a missing card). main reads+clears it to log one diagnostic.
bool        takeWriteError();

} // namespace logging
