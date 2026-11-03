#ifndef USURPER_PROJECTILE_MOTION_FAST_H
#define USURPER_PROJECTILE_MOTION_FAST_H
#include <stdint.h>
/* Exactly trunc(v*(age+1)/60)-trunc(v*age/60), including negative v.
 * For |v|=60*q+r, the increment is q plus the carry of (r*age)%60+r.
 * Only remainders are multiplied (<=59*59), so RV32 needs no 64-bit divide.
 * Projectile age is uint16_t; age+1 is promoted before the original product. */
static inline int32_t us_projectile_motion_step(int32_t velocity,uint16_t age) {
    uint32_t magnitude=velocity<0?0u-(uint32_t)velocity:(uint32_t)velocity;
    uint32_t quotient=magnitude/60u,remainder=magnitude%60u;
    uint32_t delta=quotient+((remainder*(age%60u))%60u+remainder)/60u;
    return velocity<0?-(int32_t)delta:(int32_t)delta;
}
#endif
