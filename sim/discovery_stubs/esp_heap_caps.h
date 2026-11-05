// Host stub for the discovery test: heap_caps_* map to malloc/free.
#pragma once
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static inline void* heap_caps_malloc(size_t n, uint32_t) { return std::malloc(n); }
static inline void heap_caps_free(void* p) { std::free(p); }
