// What has fired, and where.
//
// An alert lives for six seconds and then it is gone. On a drive that means
// the thing you half-noticed at a junction is unrecoverable -- you cannot go
// back and ask what it was. This keeps the last few, with the position and
// the time, so the drive can be reviewed rather than only witnessed.
#pragma once
#include <stdint.h>
#include "model.h"
#include "signatures.h"

namespace alertlog {

constexpr int DEPTH = 48;

// Record an alert. Repeats of the same threat from the same device inside
// `COALESCE_MS` extend the existing entry rather than adding a new one --
// otherwise parking next to one camera fills the log with one camera.
void push(const ui::AlertRec& a, uint32_t nowMs, uint32_t epoch,
          bool located, int32_t lat7, int32_t lon7);

void fill(ui::State& s, int offset, uint32_t nowMs);
uint16_t count();
void clear();

} // namespace alertlog
