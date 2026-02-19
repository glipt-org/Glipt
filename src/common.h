#ifndef glipt_common_h
#define glipt_common_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UINT8_COUNT (UINT8_MAX + 1)

#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

// Uncomment or define via -D to enable debug features:
// #define DEBUG_TRACE
// #define DEBUG_STRESS_GC

#endif
