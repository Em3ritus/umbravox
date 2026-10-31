#pragma once
#include <stdint.h>
#include <stddef.h>
namespace pcap {
// Main loop owns start/stop/service. Receive callback may only call receive().
bool start();
void stop();
// At most 16 queued records copied and one <=8 KiB append per pass.
// nowMs is monotonic; sparse batches flush after 250 ms.
void service(uint32_t nowMs);
uint32_t batches();
uint32_t peakQueue();
void receive(const uint8_t* frame,size_t bytesWithoutFcs,uint64_t uptimeUs);
bool active();
bool pending();
uint32_t written();
uint32_t dropped();
const char* status();
const char* fileName();
}
