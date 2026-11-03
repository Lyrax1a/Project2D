/* PCG-XSH-RR state/output equations and Godot 4.6's float sampling convention.
 * Reference: Godot core/math/random_pcg.h and thirdparty/misc/pcg.cpp.
 * PCG algorithm by M. E. O'Neill (pcg-random.org). This implementation uses
 * explicit unsigned arithmetic and float exponent adjustment on RV32IM. */
#include "usurper_rng.h"
#include "usurper_softfloat.h"
#include <string.h>
uint32_t us_rng_u32(us_rng_t *rng){
    uint64_t before=rng->state;rng->state=before*UINT64_C(6364136223846793005)+(rng->increment|1);
    uint32_t fraction=(uint32_t)(((before>>18)^before)>>27),shift=(uint32_t)(before>>59);
    return (fraction>>shift)|(fraction<<((32-shift)&31));
}
void us_rng_seed(us_rng_t *rng,uint64_t seed){
    rng->state=0;rng->increment=(UINT64_C(1442695040888963407)<<1)|1;
    (void)us_rng_u32(rng);rng->state+=seed;(void)us_rng_u32(rng);
}
float us_rng_float(us_rng_t *rng){
    uint32_t exponent=us_rng_u32(rng);if(!exponent)return 0;
    unsigned leading=0;for(uint32_t bit=UINT32_C(0x80000000);!(exponent&bit);bit>>=1)++leading;
    float value=(float)(us_rng_u32(rng)|UINT32_C(0x80000001));uint32_t bits;memcpy(&bits,&value,4);
    bits-=(32+leading)<<23;memcpy(&value,&bits,4);return value;
}
float us_rng_range(us_rng_t *rng,float from,float to){return us_sf_add(us_sf_mul(us_rng_float(rng),us_sf_add(to,-from)),from);}
