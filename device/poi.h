#pragma once
#include "../ui/gnss.h"
#include "../ui/poi_state.h"
namespace poi {
// Capture at the action, not when the later SD write runs. One pending mark.
bool request(const ui::Gnss::Fix& fix, uint32_t now, uint32_t epoch, const char* session);
// Main-loop only, after the logger has closed its append handle.
void service(uint32_t now);
ui::PoiStatus status(uint32_t now);
}
