// Report-time staggering: node idx of n reports at offset idx*window/n, so a
// cluster's deliveries interleave instead of colliding on the report channel.
// Pure; no hardware.
#pragma once
#include <stdint.h>
namespace mesh_phase {
inline uint32_t offset(int idx, int n, uint32_t windowMs) {
    if (n <= 1) return 0;
    return (uint32_t)((idx % n) * (uint64_t)windowMs / n);
}
}
