#ifndef USURPER_TRIANGLE_ALPHA_MATH_H
#define USURPER_TRIANGLE_ALPHA_MATH_H
#include <stdint.h>
#include "usurper_triangle_math.h"
typedef struct {uint64_t coefficient;uint32_t area;float paint;unsigned valid;} us_triangle_alpha_t;
static inline void us_triangle_alpha_prepare(us_triangle_alpha_t *out,int64_t area,float paint){
    out->valid=area>0&&area<=INT64_C(0xffffffff)&&paint>=0&&paint<=1;
    out->area=out->valid?(uint32_t)area:0;out->paint=paint;out->coefficient=0;
    if(out->valid)out->coefficient=(uint64_t)((double)paint*255.0*281474976710656.0/(double)area);
}
/* Return 1 only when the Q48 result is provably on the same side of the
 * integer boundary as the original ratio-to-binary32/multiply/multiply/add.
 * For exact 0<=n<=area<2^32 and 0<=paint<=1, those roundings change
 * the unrounded result by <2^-14. Truncating the Q48 coefficient adds
 * <2^-16, with double setup rounding <2^-38. The guard 2^-12 exceeds
 * their sum. Products are <=255*2^48, well inside uint64. Near a boundary
 * callers use the original calculation, including ties. */
static inline unsigned us_triangle_alpha_fast(const us_triangle_alpha_t *math,int64_t numerator,unsigned *alpha){
    if(!math->valid||numerator<0||numerator>math->area)return 0;
    uint64_t rounded=(uint64_t)(uint32_t)numerator*math->coefficient+(UINT64_C(1)<<47);
    uint64_t fraction=rounded&((UINT64_C(1)<<48)-1);
    if(fraction<(UINT64_C(1)<<36)||fraction>((UINT64_C(1)<<48)-(UINT64_C(1)<<36)))return 0;
    *alpha=(unsigned)(rounded>>48);return 1;
}
static inline unsigned us_triangle_paint_channel(float paint,float coverage){
    float value=us_sf_mul(paint,coverage);
    return value<=0?0:value>=1?255u:(unsigned)us_sf_add(us_sf_mul(value,255),0.5f);
}
#endif
