#ifndef USURPER_RNG_H
#define USURPER_RNG_H
#include <stdint.h>
typedef struct {uint64_t state,increment;} us_rng_t;
void us_rng_seed(us_rng_t *rng,uint64_t seed);
uint32_t us_rng_u32(us_rng_t *rng);
float us_rng_float(us_rng_t *rng);
float us_rng_range(us_rng_t *rng,float from,float to);
#endif
