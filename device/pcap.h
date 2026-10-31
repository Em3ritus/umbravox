#pragma once
#include <stdint.h>
#include <stddef.h>
namespace pcap {
// Main loop owns start/stop/service. Receive callback may only call receive().
bool start();
void stop();
void service();
void receive(const uint8_t* frame,size_t bytesWithoutFcs,uint64_t uptimeUs);
bool active();
bool pending();
uint32_t written();
uint32_t dropped();
const char* status();
const char* fileName();
}
