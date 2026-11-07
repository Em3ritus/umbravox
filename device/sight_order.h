#pragma once
#include <algorithm>
#include <stdint.h>

// Immutable RSSI snapshot during sorting; never compare live radio-owned values.
namespace sightorder {
struct Entry { uint16_t slot; int8_t rssi; };
inline void sort(Entry* rows, uint32_t count) {
    std::sort(rows, rows + count, [](const Entry& a, const Entry& b) {
        return a.rssi != b.rssi ? a.rssi > b.rssi : a.slot < b.slot;
    });
}
struct Cache {
    Entry* rows = nullptr; // caller supplies PSRAM, never internal-heap fallback
    uint32_t count = 0, at = 0, generation = 0;
    int filter = -1;
    bool due(uint32_t now, int f, uint32_t gen = 0) const {
        return generation != gen || filter != f || uint32_t(now-at) >= 1000;
    }
};
}
