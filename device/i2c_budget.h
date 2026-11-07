#pragma once
#include <cstdint>
namespace i2cbudget {
// Wrap-safe aggregate transaction allowance, shared by every protocol step.
inline uint16_t remaining(uint32_t start, uint32_t now, uint16_t budget) {
    const uint32_t elapsed = now - start;
    return elapsed < budget ? static_cast<uint16_t>(budget-elapsed) : 0;
}
}
