// Bring-up scaffolding: a State full of invented but plausible numbers.
//
// Shared by the simulator and the device so there is exactly one copy of the
// fake data. Delete this file the day a real collector fills State instead --
// nothing in ui/ should depend on it.
#pragma once
#include "model.h"

namespace ui {

State demoState(uint32_t frame);

}
