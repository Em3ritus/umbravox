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
// Zeroed-memory allocator for the per-slot change signatures (3 KB). The device points this at PSRAM; the
// default is calloc. Call it once at boot, before the first session (a later call makes the next session allocate again). If it returns null the writer still works, it just re-writes every record each sweep.
// File size limit (default 16 MiB per file). A file that reaches it continues in -2, -3 ... up to MAX_PARTS parts.
// The setter exists for host tests; the device never calls it.
constexpr unsigned MAX_PARTS=8;
void setFileCap(uint32_t bytes);
void setAllocator(void*(*zeroed)(size_t bytes));
void tick(const char* session,uint32_t now,uint32_t epoch,Source source);
void stop(); // drains the two batches; shutdown only
bool takeError();
const char* failReason(); // why the export stopped (empty if it has not): a write error, or the 16 MiB file limit
const char* status();
const char* activeFile(unsigned index); // includes stop/rollover flush
uint32_t rows(); // committed rows, not unique records
}
