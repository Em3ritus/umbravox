// Main-loop-only GPX companion to a WiGLE recording. No radio/callback I/O.
#pragma once
#include "../ui/gnss.h"
namespace gpx {
constexpr uint32_t FLUSH_MS=5000, SAMPLE_MS=1000;
// session is the CSV path while recording, otherwise "". UTC is for fix.atMs,
// not service time; zero means unknown and omits <time>.
void tick(const char* session,const ui::Gnss::Fix& fix,uint32_t now,uint32_t fixEpoch);
const char* status();
const char* fileName();
uint32_t points(); // successfully committed points, current/last session
bool takeError();
}
