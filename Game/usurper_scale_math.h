#ifndef USURPER_SCALE_MATH_H
#define USURPER_SCALE_MATH_H
#include <stdint.h>
/* Exact floor(n/15), including UINT32_MAX. 0x88888889 is ceil(2^35/15).
 * Its excess is 7/15, so the quotient error is <7/120 for n<2^32.
 * The largest remainder contributes 14/15; their sum is <1. RV32IM
 * lowers this to MULHU and a shift, avoiding its iterative divider. */
static inline uint32_t us_scale_div15(uint32_t n) {
    return (uint32_t)(((uint64_t)n * UINT64_C(0x88888889)) >> 35);
}
#endif
