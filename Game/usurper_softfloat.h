#ifndef USURPER_SOFTFLOAT_H
#define USURPER_SOFTFLOAT_H
#include <stdint.h>
/* Small IEEE float32 operations for the shader's normal finite values.
 * Exceptional/subnormal cases retain the compiler's general implementation. */
float us_sf_add(float a,float b);
float us_sf_mul(float a,float b);
float us_sf_div(float a,float b);
/* Stack-local preparation for a run of divisions by the same denominator.
 * Uses the same corrected integer quotient and ties-to-even rounding. */
typedef struct {uint32_t bits,mantissa,exponent;int64_t reciprocal;} us_sf_divisor_t;
void us_sf_div_prepare(float b,us_sf_divisor_t *divisor);
float us_sf_div_prepared(float a,const us_sf_divisor_t *divisor);
#endif
