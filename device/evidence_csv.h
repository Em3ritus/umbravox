#pragma once
#include <cstddef>
#include <cstdint>
#include "clients.h"
#include "probe_evidence.h"
namespace evidencecsv {
constexpr uint32_t INTERVAL_MS=10000;
// Main-loop source: one fixed table slot, no sorting. Empty/expired slot -> 0.
using Source=size_t(*)(unsigned slot,char* out,size_t capacity,uint32_t now,uint32_t epoch);
size_t client(char*,size_t,const clients::Record&,uint32_t,uint32_t,uint32_t drops,uint32_t evictions);
size_t probe(char*,size_t,const evidence::ProbeRow&,uint32_t,uint32_t,uint32_t drops,uint32_t evictions);
void tick(const char* session,uint32_t now,uint32_t epoch,Source source);
void stop(); // drains the two batches; shutdown only
bool takeError();
const char* status();
const char* activeFile(unsigned index); // includes stop/rollover flush
uint32_t rows(); // committed rows, not unique records
}
