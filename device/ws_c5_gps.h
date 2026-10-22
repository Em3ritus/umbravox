// Waveshare ESP32-C5-Touch-LCD-3.5: the LC76G GNSS receiver over I2C.
//
// Why I2C: the J3 header's UART holes (U0RXD, GPIO12) are not bonded to the
// chip on boards as shipped, but its SDA/SCL reach the shared bus (SDA 27 /
// SCL 26) and Waveshare's LC76G module speaks I2C. So the GPS rides the bus
// the touch panel, the expander and the PMU are already on.
//
// Why synchronous, on the main loop: an LC76G read is several transactions,
// and no other device may be addressed in the middle of one (Quectel). Run
// whole from loop(), nothing else on the bus can interleave -- touch, the
// expander and the PMU are all read from loop() too -- so none of them needs
// a lock. The price is a short stall while a cycle runs, throttled to two a
// second.
//
// Protocol and the Recovery / Wake routines follow Waveshare's LC76G_RPI_I2C
// demo.
#pragma once
#include <stdint.h>

namespace gpsi2c {

struct Stats {
    uint32_t cycles = 0;   // read cycles attempted
    uint32_t bytes  = 0;   // NMEA bytes delivered
    uint32_t empty  = 0;   // cycles where the module had nothing queued
    uint32_t fails  = 0;   // cycles that failed on the bus
    uint32_t recov  = 0;   // recovery nudges that got an ACK
    uint32_t wakes  = 0;   // wake sequences that completed
};

// After exio::begin() has opened the bus. Sizes Wire's buffer for a cycle.
void begin();

// Every loop. Self-throttles; feeds each byte read to sink(c, nowMs).
//
// The caller must not tick while a finger is on the panel: a cycle's stall,
// and the bus re-open it starts with, are what break a touch drag on this
// shared bus. touch reads are restored to their own Wire timeout after.
void tick(void (*sink)(char, uint32_t), uint32_t nowMs);

const Stats& stats();

} // namespace gpsi2c
